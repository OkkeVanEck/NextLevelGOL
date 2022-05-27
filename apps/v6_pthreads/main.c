#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>

#include "gif.h"
#include "gol.h"
#include "opts.h"
#include "util.h"

int NTHREADS = 2;

struct args {
    int id;
    options opts;
    world *cur_world;
    world *next_world;
    FILE *output_fp;
};


/* Setup pthreads according to environment variable. */
pthread_t* init_pthreads() {
    /* Create threads through environment variable. */
    const char* nthreads_str = getenv("PTHREADS_NUM_THREADS");

    /* Default to 2 threads if no variable was set. */
    if (nthreads_str != NULL) {
        NTHREADS = atoi(nthreads_str);
//        fprintf(stderr, "[PTHREADS]\tPTHREADS_NUM_THREADS set to %d\n", NTHREADS);
    } else {
//        fprintf(stderr, "[PTHREADS]\tPTHREADS_NUM_THREADS not set, defaulting to %d\n", NTHREADS);
    }

    pthread_t* threads = malloc(sizeof(pthread_t) * NTHREADS);
    return threads;
}

/* Join and destroy all threads. */
void close_pthreads(pthread_t* threads) {
//    fprintf(stderr, "\n[PTHREADS]\tJoining threads\n");

    /* Join the threads. */
    for (int t = 0; t < NTHREADS; ++t) {
        pthread_join(threads[t], NULL);
    }

//    fprintf(stderr, "[PTHREADS]\tFreeing memory\n\n");

    /* Free the threads. */
    free(threads);
}

/* Entry point for a thread to run its part of the simulation WITHOUT timing. */
void *run_simulation(void *args) {
    world *tmp_world;

    /* Unpack variables from arguments. */
    int id = ((struct args *)args)->id;
    options opts = ((struct args *)args)->opts;
    world *cur_world = ((struct args *)args)->cur_world;
    world *next_world = ((struct args *)args)->next_world;
    FILE *output_fp = ((struct args *)args)->output_fp;

    /* Run time steps. */
    if (id != 0) {
        return;
    }

    /* Run time steps. */
    for (int n = 0; n < opts.steps; n++) {
        world_border_timestep(cur_world, next_world);
        world_timestep(cur_world, next_world);

        /* Swap old and new worlds. */
        tmp_world = cur_world;
        cur_world = next_world;
        next_world = tmp_world;

        if (opts.verbose != 0) {
            printf("World contains %d live cells after time step %d:\n\n", world_count(cur_world), n);
            world_print(cur_world);
        }

        if (opts.use_output != 0) {
            write_gif_frame(cur_world->width, cur_world->height, cur_world->cells[0], output_fp);
        }
    }

    return NULL;
}

/* Entry point for a thread to run its part of the simulation WITH timing. */
void *run_simulation_timed(void *args) {
    world *tmp_world;

    /* Declare timing variables. */
    struct timeval tv;
    double time_start, time_end, total;
    double wrap = 0.0;
    double step = 0.0;
    double swap = 0.0;
    double gif  = 0.0;

    /* Unpack variables from arguments. */
    int id = ((struct args *)args)->id;
    options opts = ((struct args *)args)->opts;
    world *cur_world = ((struct args *)args)->cur_world;
    world *next_world = ((struct args *)args)->next_world;
    FILE *output_fp = ((struct args *)args)->output_fp;

    /* Run time steps. */
    if (id != 0) {
        return;
    }

    for (int n = 0; n < opts.steps; n++) {
        time_start = time_secs(tv);
        world_border_timestep(cur_world, next_world);
        time_end = time_secs(tv);
        wrap += time_end - time_start;

        time_start = time_secs(tv);
        world_timestep(cur_world, next_world);
        time_end = time_secs(tv);
        step += time_end - time_start;

        /* Swap old and new worlds. */
        time_start = time_secs(tv);
        tmp_world = cur_world;
        cur_world = next_world;
        next_world = tmp_world;
        time_end = time_secs(tv);
        swap += time_end - time_start;

        time_start = time_secs(tv);

        if (opts.verbose != 0) {
            printf("World contains %d live cells after time step %d:\n\n", world_count(cur_world), n);
            world_print(cur_world);
        }

        if (opts.use_output != 0) {
            write_gif_frame(cur_world->width, cur_world->height, cur_world->cells[0], output_fp);
        }

        time_end = time_secs(tv);
        gif += time_end - time_start;
    }

    /* Print timing data */
    if (id == 0) {
        total = wrap + step + swap + gif;
        fprintf(stderr, "Total time spent in each part:\n");
        fprintf(stderr, "  wrap : %7.3f seconds (%6.2f%%)\n", wrap, wrap/total*100);
        fprintf(stderr, "  step : %7.3f seconds (%6.2f%%)\n", step, step/total*100);
        fprintf(stderr, "  swap : %7.3f seconds (%6.2f%%)\n", swap, swap/total*100);
        fprintf(stderr, "  gif  : %7.3f seconds (%6.2f%%)\n", gif, gif/total*100);
        fprintf(stderr, "  -----------------------------------\n");
        fprintf(stderr, "  total: %7.3f seconds (100.00%%)\n\n", total);

        fprintf(stderr, "Throughput: %.0f pixels/second\n", opts.width * opts.height / total);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    FILE *input_fp = NULL, *output_fp = NULL;

    world worlds[2];
    world *cur_world, *next_world;

    options opts;
    parse_opts(argc, argv, &opts);

    /* Open the input file. */
    if (opts.use_input) {
        input_fp = fopen(opts.input, "r");
        if (input_fp == NULL) {
            fprintf(stderr, "Failed to open input file: '%s'!", opts.input);
            exit(1);
        }
    }

    /* Open the output file. */
    if (opts.use_output) {
        output_fp = fopen(opts.output, "w");
        if (output_fp == NULL) {
            fprintf(stderr, "Failed to open output file: '%s'!", opts.output);
            exit(1);
        }
    }

    /* Initialize worlds. */
    worlds[0].width = opts.width;
    worlds[0].height = opts.height;
    worlds[0].cells = alloc_2d_int_array(opts.height, opts.width);

    worlds[1].width = opts.width;
    worlds[1].height = opts.height;
    worlds[1].cells = alloc_2d_int_array(opts.height, opts.width);

    cur_world = &worlds[0];
    next_world = &worlds[1];

    /* Initialize world */
    if (opts.use_input != 0) {
        world_load(cur_world, input_fp);
        fclose(input_fp);
    } else {
        world_init_random(cur_world, opts.seed);
    }

    /* Print the initial world state */
    if (opts.verbose != 0) {
        printf("\ninitial world:\n\n");
        world_print(cur_world);
    }

    /* Write the header part of the gif file */
    if (opts.use_output != 0) {
        write_gif_header(cur_world->width, cur_world->height, output_fp);
    }

    /* Setup pthreads memory. */
    pthread_t* threads = init_pthreads();

    /* Setup args for each thread. */
    struct args args[NTHREADS];

    for (int t = 0; t < NTHREADS; ++t) {
        args[t].id = t;
        args[t].opts = opts;
        args[t].cur_world = cur_world;
        args[t].next_world = next_world;
        args[t].output_fp = output_fp;
    }

    /* Based on settings, run threads with or without timing code. */
    if (opts.time_code != 0) {
        for (int t = 0; t < NTHREADS; ++t) {
            pthread_create(&threads[t], NULL, run_simulation_timed, &args[t]);
        }
    } else {
        for (int t = 0; t < NTHREADS; ++t) {
            pthread_create(&threads[t], NULL, run_simulation, &args[t]);
        }
    }

    /* Join and destroy threads. */
    close_pthreads(threads);

    /* Close the gif file */
    if (opts.use_output != 0) {
        write_gif_trailer(output_fp);
        fclose(output_fp);
    }

    return 0;
}
