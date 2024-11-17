// This Program runs by taking in a file and several integers. The file
// is used to read in parameters for a game of life simulation: the
// board size, number of iterations to run, and the number and positions
// of the starting cells. The integers determines whether to run with no
// output, ascii output, or paravisi output; how many pthreads to run the program
// with; whether to partition the gol board in rows or columns; and
// whether to print pthreads debugging info.

/*
 * To run:
 * ./gol file1.txt  0  # run with config file file1.txt, do not print board
 * ./gol file1.txt  1  # run with config file file1.txt, ascii animation
 * ./gol file1.txt  2  # run with config file file1.txt, ParaVis animation
 *
 */
#include <pthreadGridVisi.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include "colors.h"

/****************** Definitions **********************/
/* Three possible modes in which the GOL simulation can run */
#define OUTPUT_NONE (0)  // with no animation
#define OUTPUT_ASCII (1) // with ascii animation
#define OUTPUT_VISI (2)  // with ParaVis animation

/* Used to slow down animation run modes: usleep(SLEEP_USECS);
 * Change this value to make the animation run faster or slower
 */
// #define SLEEP_USECS  (1000000)
#define SLEEP_USECS (250000)

/* A global variable to keep track of the number of live cells in the
 * world (this is the ONLY global variable you may use in your program)
 */
static int total_live = 0;

/* declare a mutex: initialize in main */
static pthread_mutex_t my_mutex;

/* declare a barrier: initialize in main */
static pthread_barrier_t my_barrier;

/* This struct represents all the data you need to keep track of your GOL
 * simulation.  Rather than passing individual arguments into each function,
 * we'll pass in everything in just one of these structs.
 * this is passed to play_gol, the main gol playing loop
 *
 * NOTE: You will need to use the provided fields here, but you'll also
 *       need to add additional fields. (note the nice field comments!)
 * NOTE: DO NOT CHANGE THE NAME OF THIS STRUCT!!!!
 */
struct gol_data
{

    // NOTE: DO NOT CHANGE the names of these 4 fields (but USE them)
    int rows;           // the row dimension
    int cols;           // the column dimension
    int iters;          // number of iterations to run the gol simulation
    int output_mode;    // set to:  OUTPUT_NONE, OUTPUT_ASCII, or OUTPUT_VISI
    int starting_cells; // number of starting live cells
    int *current;
    int *next; // pointers for game board
    int temp1; // temps to read in cell coordinates
    int temp2;
    int curr_iter;
    int num_threads;
    int partition;
    int print_partition;
    int c_start;
    int c_end;
    int r_start;
    int r_end;
    int local_id;

    /* fields used by ParaVis library (when run in OUTPUT_VISI mode). */
    // NOTE: DO NOT CHANGE their definitions BUT USE these fields
    visi_handle handle;
    color3 *image_buff;
};

/****************** Function Prototypes **********************/

/* the main gol game playing loop (prototype must match this) */
void *play_gol(void *args);

/* init gol data from the input file and run mode cmdline args */
int init_game_data_from_args(struct gol_data *data, char **argv);

// A mostly implemented function, but a bit more for you to add.
/* print board to the terminal (for OUTPUT_ASCII mode) */
void print_board(struct gol_data *data, int round);

// epic function
int do_pthreads_stuff(struct gol_data *data);

/* Mainly a function for counting number of live cells
when running with do not print board*/
void none_board(struct gol_data *data, int round);

// copy contents of "next" into "current."
void copy_over(struct gol_data *data);

void check_inner(struct gol_data *data);
void check_edges(struct gol_data *data);
void check_corners(struct gol_data *data);
void reverse_copyover(struct gol_data *data);

void advanceGrid(struct gol_data *data);

void update_colors(struct gol_data *data);

/************ Definitions for using ParVisi library ***********/
/* initialization for the ParaVisi library (DO NOT MODIFY) */
int setup_animation(struct gol_data *data);
/* register animation with ParaVisi library (DO NOT MODIFY) */
int connect_animation(void (*applfunc)(struct gol_data *data),
                      struct gol_data *data);
/* name for visi (you may change the string value if you'd like) */
static char visi_name[] = "GOL!";

int main(int argc, char **argv)
{
    int ret;
    struct gol_data data;
    struct timeval start_time, stop_time;
    float smallsec; // smallsec will hold our final microseconds.
    double secs;    // secs will hold our final run time.

    /* check number of command line arguments */
    if (argc < 6)
    {
        printf("usage: %s <infile.txt> <output_mode>[0|1|2] <num_threads> <partition> <print_partition>\n", argv[0]);
        printf("(0: file, 1:OUTPUTMODE , 2: ParaVisi)\n");
        exit(1);
    }

    data.output_mode = atoi(argv[2]);
    data.num_threads = atoi(argv[3]);
    data.partition = atoi(argv[4]);
    data.print_partition = atoi(argv[5]);

    if (data.num_threads < 1)
    {
        printf("Error: Invalid number of threads.\n");
        exit(1);
    }

    /* Initialize game state (all fields in data) from information
     * read from input file */
    ret = init_game_data_from_args(&data, argv);
    if (ret != 0)
    {
        printf("Initialization error: file %s, mode %s\n", argv[1], argv[2]);
        exit(1);
    }

    // Initialize the mutex
    if (pthread_mutex_init(&my_mutex, NULL))
    {
        printf("pthread_mutex_init error\n");
        exit(1);
    }

    // Initialize the barrier with num threads that will be synchronized
    if (pthread_barrier_init(&my_barrier, NULL, data.num_threads))
    {
        printf("pthread_barrier_init error\n");
        exit(1);
    }

    /* Invoke play_gol in different ways based on the run mode */
    if (data.output_mode == OUTPUT_NONE)
    { // run with no animation
        ret = gettimeofday(&start_time, NULL);
        do_pthreads_stuff(&data);
        ret = gettimeofday(&stop_time, NULL);
    }
    else if (data.output_mode == OUTPUT_ASCII)
    { // run with ascii animation

        if (system("clear"))
        {
            perror("clear");
            exit(1);
        }

        ret = gettimeofday(&start_time, NULL);
        reverse_copyover(&data);
        print_board(&data, 0);
        do_pthreads_stuff(&data);
        ret = gettimeofday(&stop_time, NULL);

        // clear the previous print_board output from the terminal:
        // (NOTE: you can comment out this line while debugging)

        // NOTE: DO NOT modify this call to print_board at the end
        //       (it's to help us with grading your output)
        print_board(&data, data.iters);
    }
    else
    { // OUTPUT_VISI: run with ParaVisi animation
      // tell ParaVisi that it should run play_gol
        // connect_animation(play_gol, &data);
        //  start ParaVisi animation
        setup_animation(&data);
        reverse_copyover(&data);
        do_pthreads_stuff(&data);
    }

    // NOTE: you need to determine how and where
    // to add timing code
    //       in your program to measure the total time to play the given
    //       number of rounds played.
    if (data.output_mode != OUTPUT_VISI)
    {
        secs = 0.0;

        // convert our microseconds into a float
        smallsec = stop_time.tv_usec;
        smallsec -= start_time.tv_usec;
        smallsec = smallsec / 1000000;

        secs += stop_time.tv_sec;
        secs -= start_time.tv_sec;

        // now we have total runtime.
        secs = secs + smallsec;

        /* Print the total runtime, in seconds. */
        // NOTE: do not modify these calls to fprintf
        fprintf(stdout, "Total time: %0.3f seconds\n", secs);
        fprintf(stdout, "Number of live cells after %d rounds: %d\n\n",
                data.iters, total_live);
    }

    if (pthread_mutex_destroy(&my_mutex))
    {
        printf("pthread_mutex_destroy error\n");
        exit(1);
    }

    if (pthread_barrier_destroy(&my_barrier))
    {
        printf("pthread_barrier_destroy error\n");
        exit(1);
    }

    // clean-up before exit
    free(data.current);
    free(data.next);

    return 0;
}

/* initialize the gol game state from command line arguments
 *       argv[1]: name of file to read game config state from
 *       argv[2]: run mode value
 * data: pointer to gol_data struct to initialize
 * argv: command line args
 *       argv[1]: name of file to read game config state from
 *       argv[2]: run mode
 * returns: 0 on success, 1 on error
 */
int init_game_data_from_args(struct gol_data *data, char **argv)
{

    FILE *infile;
    int ret;
    int i;

    infile = fopen(argv[1], "r");
    if (infile == NULL)
    {
        printf("Error: Failed to open file: %s\n", argv[0]);
        return (1);
    }

    ret = fscanf(infile, "%d", &data->rows);
    if (ret == 0)
    {
        printf("Improper file format.1\n");
        return (1);
    }

    ret = fscanf(infile, "%d", &data->cols);
    if (ret == 0)
    {
        printf("Improper file format.2\n");
        return (1);
    }

    if (data->cols < 4)
    {
        printf("The entered board size is too small.\n");
        return (1);
    }
    else if (data->rows < 4)
    {
        printf("The entered board size is too small.\n");
        return (1);
    } // these if statements check the minimum board size.

    ret = fscanf(infile, "%d", &data->iters);
    if (ret == 0)
    {
        printf("Improper file format.\n");
        return (1);
    }

    ret = fscanf(infile, "%d", &data->starting_cells);
    if (ret == 0)
    {
        printf("Improper file format.\n");
        exit(1);
    }

    data->current = malloc(sizeof(int) * data->rows * data->cols);
    if (!data->current)
    {
        printf("Malloc Failed.\n");
        exit(1);
    }

    data->next = malloc(sizeof(int) * data->rows * data->cols);
    if (!data->next)
    {
        printf("Malloc Failed.\n");
        exit(1);
    }

    for (i = 0; i < data->cols * data->rows; i++)
    {
        data->current[i] = 0; // we start by setting every bucket to 0.
        // data->next[i] = 0;
    }

    for (i = 0; i < data->starting_cells; i++)
    {
        ret = fscanf(infile, "%d%d", &data->temp1, &data->temp2);
        if (ret == 0)
        {
            printf("Improper file format.\n");
            return (1);
        }

        // initializing the board by setting the starting cells to 1.
        data->current[data->temp1 * data->cols + data->temp2] = 1;
    }

    ret = fclose(infile);
    if (ret != 0)
    {
        printf("Error: failed to close file: %s\n", argv[1]);
        exit(1);
    }

    return 0;
}

/* function get_pthread_info: determines which section of the board each
 * pthread needs to work on. Takes in user cmd line info and creates an
 * array of type struct pthreadworker.
 */

int do_pthreads_stuff(struct gol_data *data)
{
    int div;
    int div_ext;

    struct gol_data *info = NULL;
    info = malloc(sizeof(struct gol_data) * data->num_threads);
    pthread_t *tid = malloc(sizeof(pthread_t) * data->num_threads);

    // Handles error when mallocing info and tid
    if (!info)
    {
        perror("malloc: pthread array\n");
        exit(1);
    }
    if (!tid)
    {
        perror("malloc: pthread array\n");
        exit(1);
    }

    /*
    Loop num_thread times and calculate the portion of board
    to assign to each thread.
    */
    for (int i = 0; i < data->num_threads; i++)
    {

        // print_partition and id set for each thread
        info[i] = *data;
        info[i].local_id = i;
        info[i].print_partition = data->print_partition;

        if (!data->partition)
        {
            div = data->rows / data->num_threads;
            div_ext = data->rows % data->num_threads;
            info[i].r_start = i * div;
            if (i < div_ext)
            {
                info[i].r_start += i;
            }
            else
            {
                info[i].r_start += div_ext;
            }
            info[i].r_end = info[i].r_start + div;
            if (!(i < div_ext))
            {
                info[i].r_end--;
            }
            info[i].c_start = 0;
            info[i].c_end = data->cols - 1;
        }
        else
        {
            // same as above but swap all cols with rows.
            div = data->cols / data->num_threads;
            div_ext = data->cols % data->num_threads;
            info[i].c_start = i * div;
            if (i < div_ext)
            {
                info[i].c_start += i;
            }
            else
            {
                info[i].c_start += div_ext;
            }
            info[i].c_end = info[i].c_start + div;
            if (!(i < div_ext))
            {
                info[i].c_end--;
            }
            info[i].r_start = 0;
            info[i].r_end = data->rows - 1;
        }

        // make new thread
        if (pthread_create(&tid[i], NULL, play_gol, &info[i]))
        {
            printf("pthread_create failed on thread %d\n", i);
            exit(1);
        }
    }

    if (data->output_mode == OUTPUT_VISI)
    {
        run_animation(data->handle, data->iters);
    }
    // wait for thread to finish
    for (int i = 0; i < data->num_threads; i++)
    {
        pthread_join(tid[i], NULL);
    }

    // free tid and info
    free(tid);
    free(info);

    return 0;
}

/* the gol application main loop function:
 *  runs rounds of GOL,
 *    * updates program state for next round (world and total_live)
 *    * performs any animation step based on the output/run mode
 *
 *   Print the board to the terminal.
 *   data: gol game specific data
 *   round: the current round number

 *   data: pointer to a struct gol_data  initialized with
 *         all GOL game playing state
 */
void *play_gol(void *args)
{

    struct gol_data *data;

    data = (struct gol_data *)args;

    // print out partioning information if last cmd argument is 0
    if (data->print_partition != 0)
    {
        printf("tid  %d: rows:  %d:%d (%d) cols:  %d:%d (%d)\n",
               data->local_id, data->r_start, data->r_end,
               data->r_end - data->r_start + 1,
               data->c_start, data->c_end,
               data->c_end - data->c_start + 1);
    }

    int round;
    int ret;

    for (round = 1; round <= data->iters; round++)
    {

        total_live = 0;

        ret = pthread_barrier_wait(&my_barrier);
        if (ret != 0 && ret != PTHREAD_BARRIER_SERIAL_THREAD)
        {
            perror("pthread_barrier_wait");
            exit(1);
        }

        // Checks neighbor and update accordingly
        advanceGrid(data);

        if (data->output_mode == OUTPUT_ASCII)
        {
            // If ASCII: pause and then print board.
            if (data->local_id == 0)
            {

                if (system("clear"))
                {
                    perror("clear");
                    exit(1);
                }

                print_board(data, round);
                usleep(SLEEP_USECS);

                if (system("clear"))
                {
                    perror("clear");
                    exit(1);
                }
            }
        }
        else if (data->output_mode == OUTPUT_VISI)
        {
            // If VISI: pause and then update board.
            usleep(SLEEP_USECS);
            update_colors(data);
            draw_ready(data->handle);
        }

        copy_over(data); // copy next into current for the next round.
        ret = pthread_barrier_wait(&my_barrier);
        if (ret != 0 && ret != PTHREAD_BARRIER_SERIAL_THREAD)
        {
            perror("pthread_barrier_wait");
            exit(1);
        }
    }

    return NULL;
}

/*   For ASCII output:
 *   Print the board to the terminal.
 *   data: gol game specific data
 *   round: the current round number.
 *
 */

void print_board(struct gol_data *data, int round)
{

    int i, j;

    /* Print the round number. */
    fprintf(stderr, "Round: %d\n", round);

    for (i = 0; i < data->rows; ++i)
    {
        for (j = 0; j < data->cols; ++j)
        {
            if (data->next[i * data->cols + j] == 1)
            {
                fprintf(stderr, " @");
            }
            else
            {
                fprintf(stderr, " .");
            }
        }
        printf("\n");
    }

    /* Print the total number of live cells. */
    fprintf(stderr, "Live cells: %d\n\n", total_live);
}


/*This small function is called at the end of each iteration of GOL
 *and copies the contents of next into current such that the next
 *round can function properly.*/
void copy_over(struct gol_data *data)
{
    // swap pointers
    int *temp = data->current;
    data->current = data->next;
    data->next = temp;
}

/* This function does the exact opposite of copy_over, copying next into
 * current. It is only called once at the beginning of the program with
 * the goal of ensuring next and current are identical at the beginning
 * so the initial printboard prints the correct starting board. */

void reverse_copyover(struct gol_data *data)
{
    int i;
    for (i = 0; i < data->rows * data->cols; i++)
    {
        data->next[i] = data->current[i];
    }
}

/* Check status of the cells through intsead of manually typing
 * all eight directions, we store them in array as offset and
 * loop through the array and apply offset to the current cell.
 * To allow for checking wrapped around neighbors we apply: 
 * cur_row = (i + offset[directions][0] + row) % row
 * same applies for column
 * 
 * Then it decides whether current cell stay alive or die in
 * the next round, depending on its own status and number of living
 * neighbior cells.
 *   */

void advanceGrid(struct gol_data *data)
{

    // Just for readability
    int row = data->rows;
    int col = data->cols;

    for (int i = data->r_start; i <= data->r_end; i++)
    {
        for (int j = data->c_start; j <= data->c_end; j++)
        {

            /*
                Use an array to store offset instead
                of typing them all out, (x,y)
            */
            int offset[8][2] = {
                {-1, 0},
                {1, 0},
                {0, -1},
                {0, 1},
                {-1, -1},
                {-1, 1},
                {1, -1},
                {1, 1}};

            int current_index = i * col + j;
            int count = 0;

            /*Loop through all the offset and apply it to the current
            index*/
            for (int directions = 0; directions < 8; directions++)
            {

                int cur_row = (i + offset[directions][0] + row) % row;
                int cur_col = (j + offset[directions][1] + col) % col;

                int neighbor_index = cur_row * col + cur_col;

                // count number of neighbor cells alive
                count += data->current[neighbor_index];
            }

            // Handle when the current cell is alive
            if (data->current[current_index] == 1)
            {
                if (count == 2 || count == 3)
                {
                    data->next[current_index] = 1;
                    pthread_mutex_lock(&my_mutex);
                    total_live++;
                    pthread_mutex_unlock(&my_mutex);
                }
                else
                {
                    data->next[current_index] = 0;
                }
            }
            // when current cell not alive
            else
            {
                if (count == 3)
                {
                    data->next[current_index] = 1;
                    pthread_mutex_lock(&my_mutex);
                    total_live++;
                    pthread_mutex_unlock(&my_mutex);
                }
                else
                {
                    data->next[current_index] = 0;
                }
            }
        }
    }
}

/* this function was taken and adapted from the weeklylab exercises to be
 * used in this lab. */
void update_colors(struct gol_data *data)
{

    int i, j, r_start, r_end, c_start, c_end, index, buff_i;
    color3 *buff;

    buff = data->image_buff; // just for readability
    r_start = data->r_start;
    r_end = data->r_end;
    c_start = data->c_start;
    c_end = data->c_end;

    for (i = r_start; i <= r_end; i++)
    {
        for (j = c_start; j <= c_end; j++)
        {
            index = i * data->cols + j;
            // translate row index to y-coordinate value because in
            // the image buffer, (r,c)=(0,0) is the _lower_ left but
            // in the grid, (r,c)=(0,0) is _upper_ left.
            buff_i = (data->rows - (i + 1)) * data->cols + j;

            // update animation buffer
            // printf("%d", data->next[index]);
            if (data->next[index] == 1)
            {
                buff[buff_i] = c3_white;
            }
            else if (data->next[index] == 0)
            {
                
                buff[buff_i] = colors[data->local_id];
            }
        }
    }
}


/* initialize ParaVisi animation */
int setup_animation(struct gol_data *data)
{
    /* connect handle to the animation */
    int num_threads = data->num_threads;
    data->handle = init_pthread_animation(num_threads, data->rows,
                                          data->cols, visi_name);
    if (data->handle == NULL)
    {
        printf("ERROR init_pthread_animation\n");
        exit(1);
    }
    // get the animation buffer
    data->image_buff = get_animation_buffer(data->handle);
    if (data->image_buff == NULL)
    {
        printf("ERROR get_animation_buffer returned NULL\n");
        exit(1);
    }
    return 0;
}

/* sequential wrapper functions around ParaVis library functions */
void (*mainloop)(struct gol_data *data);

void *seq_do_something(void *args)
{
    mainloop((struct gol_data *)args);
    return 0;
}

