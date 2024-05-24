// Author: APD team, except where source was noted

#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define CONTOUR_CONFIG_COUNT    16
#define FILENAME_MAX_SIZE       50
#define STEP                    8
#define SIGMA                   200
#define RESCALE_X               2048
#define RESCALE_Y               2048

#define CLAMP(v, min, max) if(v < min) { v = min; } else if(v > max) { v = max; }

// structure containing the data needed for each image + barrier
typedef struct {
    ppm_image *image;
    ppm_image **contour_map;
    ppm_image *scaled_image;
    int step_x;
    int step_y;
    unsigned char **grid;
    pthread_barrier_t *barrier;
} data;

// structure containing the data needed for each thread
typedef struct {
    data *img_data;
    int thread_id;
    int no_threads;
} thread_data;

// Creates a map between the binary configuration (e.g. 0110_2) and the corresponding pixels
// that need to be set on the output image. An array is used for this map since the keys are
// binary numbers in 0-15. Contour images are located in the './contours' directory.
void init_contour_map(data *data) {
    for (int i = 0; i < CONTOUR_CONFIG_COUNT; i++) {
        char filename[FILENAME_MAX_SIZE];
        sprintf(filename, "./contours/%d.ppm", i);
        data->contour_map[i] = read_ppm(filename);
    }
}

// Updates a particular section of an image with the corresponding contour pixels.
// Used to create the complete contour image.
void update_image(ppm_image *image, ppm_image *contour, int x, int y) {
    for (int i = 0; i < contour->x; i++) {
        for (int j = 0; j < contour->y; j++) {
            int contour_pixel_index = contour->x * i + j;
            int image_pixel_index = (x + i) * image->y + y + j;

            image->data[image_pixel_index].red = contour->data[contour_pixel_index].red;
            image->data[image_pixel_index].green = contour->data[contour_pixel_index].green;
            image->data[image_pixel_index].blue = contour->data[contour_pixel_index].blue;
        }
    }
}

// Corresponds to step 1 of the marching squares algorithm, which focuses on sampling the image.
// Builds a p x q grid of points with values which can be either 0 or 1, depending on how the
// pixel values compare to the `sigma` reference value. The points are taken at equal distances
// in the original image, based on the `step_x` and `step_y` arguments.
void sample_grid(thread_data *t_data) {
    data *img = t_data->img_data;
    ppm_image *scaled_img = img->scaled_image;
    int p = img->scaled_image->x / img->step_x;
    int q = img->scaled_image->y / img->step_y;

    int start = t_data->thread_id * p / t_data->no_threads;
    int end =  (t_data->thread_id + 1) * p / t_data->no_threads;
    if (t_data->thread_id == t_data->no_threads - 1) {
        end = p;
    }

    for (int i = start; i < end; i++) {
        for (int j = 0; j < q; j++) {
            ppm_pixel curr_pixel = scaled_img->data[i * img->step_x * scaled_img->y + j * img->step_y];

            unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

            if (curr_color > SIGMA) {
                img->grid[i][j] = 0;
            } else {
                img->grid[i][j] = 1;
            }
        }
    }

    img->grid[p][q] = 0;

    for (int i = 0; i < p; i++) {
        ppm_pixel curr_pixel = scaled_img->data[i * img->step_x * scaled_img->y + scaled_img->x - 1];

        unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

        if (curr_color > SIGMA) {
            img->grid[i][q] = 0;
        } else {
            img->grid[i][q] = 1;
        }
    }
    for (int j = 0; j < q; j++) {
        ppm_pixel curr_pixel = scaled_img->data[(scaled_img->x - 1) * scaled_img->y + j * img->step_y];

        unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

        if (curr_color > SIGMA) {
            img->grid[p][j] = 0;
        } else {
            img->grid[p][j] = 1;
        }
    }

    pthread_barrier_wait(t_data->img_data->barrier);
}

// Corresponds to step 2 of the marching squares algorithm, which focuses on identifying the
// type of contour which corresponds to each subgrid. It determines the binary value of each
// sample fragment of the original image and replaces the pixels in the original image with
// the pixels of the corresponding contour image accordingly.
void march(thread_data *t_data) {
    data *img = t_data->img_data;
    int p = img->scaled_image->x / img->step_x;
    int q = img->scaled_image->y / img->step_y;

    int start = t_data->thread_id * p / t_data->no_threads;
    int end =  (t_data->thread_id + 1) * p / t_data->no_threads;
    if (t_data->thread_id == t_data->no_threads - 1) {
        end = p;
    }

    for (int i = start; i < end; i++) {
        for (int j = 0; j < q; j++) {
            unsigned char k = 8 * img->grid[i][j] + 4 * img->grid[i][j + 1] + 2 * img->grid[i + 1][j + 1] + 1 * img->grid[i + 1][j];
            update_image(img->scaled_image, img->contour_map[k], i * img->step_x, j * img->step_y);
        }
    }

    pthread_barrier_wait(img->barrier);
}
// Calls `free` method on the utilized resources.
void free_resources(ppm_image *image, ppm_image **contour_map, unsigned char **grid, int step_x) {
    for (int i = 0; i < CONTOUR_CONFIG_COUNT; i++) {
        free(contour_map[i]->data);
        free(contour_map[i]);
    }
    free(contour_map);

    for (int i = 0; i <= image->x / step_x; i++) {
        free(grid[i]);
    }
    free(grid);

    free(image->data);
    free(image);
}

void rescale_image(thread_data *data) {
    uint8_t sample[3];
    ppm_image *image = data->img_data->image;
    ppm_image *img_scaled = data->img_data->scaled_image;

    int start = data->thread_id * (img_scaled->x / data->no_threads);
    int end =  (data->thread_id + 1) * (img_scaled->x / data->no_threads);
    if (data->thread_id == data->no_threads - 1) {
        end = img_scaled->x;
    }

    // use bicubic interpolation for scaling
    for (int i = start; i < end; i++) {
        for (int j = 0; j < img_scaled->y; j++) {
            float u = (float)i / (float)(img_scaled->x - 1);
            float v = (float)j / (float)(img_scaled->y - 1);
            sample_bicubic(image, u, v, sample);

            img_scaled->data[i * img_scaled->y + j].red = sample[0];
            img_scaled->data[i * img_scaled->y + j].green = sample[1];
            img_scaled->data[i * img_scaled->y + j].blue = sample[2];
        }
    }

    pthread_barrier_wait(data->img_data->barrier);
}

void *thread_function(void *arg) {
    thread_data *thread_data = arg;
    ppm_image *image = thread_data->img_data->image;
    data *img_data = thread_data->img_data;

    // 1. Rescale the image
    if (!(image->x <= RESCALE_X && image->y <= RESCALE_Y)) {
        rescale_image(thread_data);
    } else {
        img_data->scaled_image = image;

    }

    // 2. Sample the grid
    sample_grid(thread_data);

    // 3. March the squares
    march(thread_data);

    pthread_exit(NULL);
}

void initialize_image_data_and_barrier(data *data_img, ppm_image *image, int step_x, int step_y) {
    ppm_image **map = (ppm_image **)malloc(CONTOUR_CONFIG_COUNT * sizeof(ppm_image *));
    if (!map) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }

    int p = image->x / step_x;
    int q = image->y / step_y;

    unsigned char **grid = (unsigned char **)malloc((p + 1) * sizeof(unsigned char*));
    if (!grid) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }

    for (int i = 0; i <= p; i++) {
        grid[i] = (unsigned char *)malloc((q + 1) * sizeof(unsigned char));
        if (!grid[i]) {
            fprintf(stderr, "Unable to allocate memory\n");
            exit(1);
        }
    }

    ppm_image *new_image = (ppm_image *)malloc(sizeof(ppm_image));
    if (!new_image) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }
    new_image->x = RESCALE_X;
    new_image->y = RESCALE_Y;

    new_image->data = (ppm_pixel*)malloc(new_image->x * new_image->y * sizeof(ppm_pixel));
    if (!new_image) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }

    data_img->image = image;
    data_img->contour_map = map;
    data_img->scaled_image = new_image;
    data_img->step_x = step_x;
    data_img->step_y = step_y;
    data_img->grid = grid;
    data_img->barrier = malloc(sizeof(pthread_barrier_t));
    if (!data_img->barrier) {
        fprintf(stderr, "Unable to allocate memory\n");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: ./tema1 <in_file> <out_file> <P>\n");
        return 1;
    }

    int no_threads = atoi(argv[3]);
    pthread_t tid[no_threads];
    data *data_img = (data *)malloc(sizeof(data));
    ppm_image *image = read_ppm(argv[1]);
    int step_x = STEP;
    int step_y = STEP;

    initialize_image_data_and_barrier(data_img, image, step_x, step_y);

    // 0. Initialize contour map
    init_contour_map(data_img);

    pthread_barrier_init(data_img->barrier, NULL, no_threads);
    for (int i = 0; i < no_threads; i++) {
        thread_data *thread_data = malloc(sizeof(thread_data));
        thread_data->img_data = data_img;
        thread_data->thread_id = i;
        thread_data->no_threads = no_threads;
        tid[i] = i;
        pthread_create(&(tid[i]), NULL, thread_function, thread_data);
    }

    for (int i = 0; i < no_threads; i++) {
        pthread_join(tid[i], NULL);
    }

    pthread_barrier_destroy(data_img->barrier);
    
    // 4. Write output
    write_ppm(data_img->scaled_image, argv[2]);

    // 5. Free resources
    free_resources(data_img->scaled_image, data_img->contour_map, data_img->grid, step_x);
    free(data_img->barrier);
    free(data_img);

    return 0;
}
