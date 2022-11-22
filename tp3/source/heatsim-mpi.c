#include <assert.h>
#include <stddef.h>

#include "heatsim.h"
#include "log.h"

int heatsim_init(heatsim_t* heatsim, unsigned int dim_x, unsigned int dim_y) {
    /*
     * TODO: Initialiser tous les membres de la structure `heatsim`.
     *       Le communicateur doit être périodique. Le communicateur
     *       cartésien est périodique en X et Y.
     */
    int err;

    int dims[2] = {dim_x, dim_y};
    const int periodic[2] = {1, 1};

    // Get rank & rank count
    err = MPI_Comm_rank(MPI_COMM_WORLD, &heatsim->rank);
    if (err != MPI_SUCCESS) {
        printf("Could not get rank\n");
        goto fail_exit;
    }

    err = MPI_Comm_size(MPI_COMM_WORLD, &heatsim->rank_count);
    if (err != MPI_SUCCESS) {
        printf("Could not get size\n");
        goto fail_exit;
    }

    // Create Cartesian topology
    err = MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periodic, 0,
                          &heatsim->communicator);
    if (err != MPI_SUCCESS) {
        printf("Could not create cartesian topology\n");
        goto fail_exit;
    }

    // Get current node coords
    err = MPI_Cart_coords(heatsim->communicator, heatsim->rank, 2,
                          heatsim->coordinates);
    if (err != MPI_SUCCESS) {
        printf("Could not create get cartesian coords\n");
        goto fail_exit;
    }

    // Get adjacent nodes
    err = MPI_Cart_shift(heatsim->communicator, 0, 1, &heatsim->rank_west_peer,
                         &heatsim->rank_east_peer);
    if (err != MPI_SUCCESS) {
        printf("Could not get horizontal shift\n");
        goto fail_exit;
    }
    err = MPI_Cart_shift(heatsim->communicator, 1, 1, &heatsim->rank_south_peer,
                         &heatsim->rank_north_peer);
    if (err != MPI_SUCCESS) {
        printf("Could not get vertical shift\n");
        goto fail_exit;
    }

    return 0;

fail_exit:
    return -1;
}

int create_data_struct(unsigned int width, unsigned int height,
                       MPI_Datatype* data_struct) {
    int err;

    int data_len = width * height;
    MPI_Datatype data_types = MPI_DOUBLE;
    MPI_Aint data_offsets = 0;

    err = MPI_Type_create_struct(1, &data_len, &data_offsets, &data_types,
                                 data_struct);
    if (err != MPI_SUCCESS) {
        printf("Could create struct data_struct\n");
        goto fail_exit;
    }

    err = MPI_Type_commit(data_struct);
    if (err != MPI_SUCCESS) {
        printf("Could not create commit type data_struct\n");
        goto fail_exit;
    }

    return 0;

fail_exit:
    return -1;
}

int create_grid_struct(MPI_Datatype* grid_struct) {
    int err;

    int grid_len[] = {1, 1, 1};
    MPI_Datatype grid_types[] = {MPI_UNSIGNED, MPI_UNSIGNED, MPI_UNSIGNED};
    MPI_Aint grid_offsets[] = {offsetof(grid_t, width),
                               offsetof(grid_t, height),
                               offsetof(grid_t, padding)};
    err = MPI_Type_create_struct(3, grid_len, grid_offsets, grid_types,
                                 grid_struct);
    if (err != MPI_SUCCESS) {
        printf("Could create struct grid_struct\n");
        goto fail_exit;
    }

    err = MPI_Type_commit(grid_struct);
    if (err != MPI_SUCCESS) {
        printf("Could not create commit type grid_struct\n");
        goto fail_exit;
    }

    return 0;

fail_exit:
    return -1;
}

int heatsim_send_grids(heatsim_t* heatsim, cart2d_t* cart) {
    /*
     * TODO: Envoyer toutes les `grid` aux autres rangs. Cette fonction
     *       est appelé pour le rang 0. Par exemple, si le rang 3 est à la
     *       coordonnée cartésienne (0, 2), alors on y envoit le `grid`
     *       à la position (0, 2) dans `cart`.
     *
     *       Il est recommandé d'envoyer les paramètres `width`, `height`
     *       et `padding` avant les données. De cette manière, le receveur
     *       peut allouer la structure avec `grid_create` directement.
     *
     *       Utilisez `cart2d_get_grid` pour obtenir la `grid` à une coordonnée.
     */
    int err;

    // grid metadata

    MPI_Datatype grid_struct;

    err = create_grid_struct(&grid_struct);
    if (err != 0) {
        goto fail_exit;
    }

    MPI_Datatype data_struct;
    unsigned int last_size = -1;
    MPI_Request req;
    MPI_Status status;

    // Send each grid
    for (int rank = 1; rank < heatsim->rank_count; ++rank) {
        int coords[2];
        // Get peer coords
        err = MPI_Cart_coords(heatsim->communicator, rank, 2, coords);
        if (err != MPI_SUCCESS) {
            printf("Could not get coords of node %d shift\n", rank);
            goto fail_exit;
        }

        grid_t* grid = cart2d_get_grid(cart, coords[0], coords[1]);

        // `data` struct
        if (grid->height * grid->width != last_size) {
            err = create_data_struct(grid->width, grid->height, &data_struct);
            if (err != 0) {
                goto fail_exit;
            }
            last_size = grid->height * grid->width;
        }

        err = MPI_Isend(grid, 1, grid_struct, rank, rank, heatsim->communicator,
                        &req);
        if (err != 0) {
            printf("Could not send grid metadata (MPI_Isend)\n");
            goto fail_exit;
        }

        // Wait for completion
        err = MPI_Wait(&req, &status);
        if (err != 0) {
            printf("Could not wait for grid metadata send (MPI_Wait)\n");
            goto fail_exit;
        }

        err = MPI_Isend(grid->data, 1, data_struct, rank, rank,
                        heatsim->communicator, &req);
        if (err != 0) {
            printf("Could not send grid data (MPI_Isend)\n");
            goto fail_exit;
        }

        // Wait for completion
        err = MPI_Wait(&req, &status);
        if (err != 0) {
            printf("Could not wait for grid data send (MPI_Wait)\n");
            goto fail_exit;
        }
    }

    return 0;

fail_exit:
    return -1;
}

grid_t* heatsim_receive_grid(heatsim_t* heatsim) {
    /*
     * TODO: Recevoir un `grid ` du rang 0. Il est important de noté que
     *       toutes les `grid` ne sont pas nécessairement de la même
     *       dimension (habituellement ±1 en largeur et hauteur). Utilisez
     *       la fonction `grid_create` pour allouer un `grid`.
     *
     *       Utilisez `grid_create` pour allouer le `grid` à retourner.
     */
    int err;
    grid_t grid_metadata;
    MPI_Request req;
    MPI_Status status;

    // grid metadata
    MPI_Datatype grid_struct, data_struct;

    err = create_grid_struct(&grid_struct);
    if (err != 0) {
        goto fail_exit;
    }

    // Receive grid using Irecv (then waiting)
    err = MPI_Irecv(&grid_metadata, 1, grid_struct, 0, heatsim->rank,
                    heatsim->communicator, &req);
    if (err != 0) {
        printf("Could not receive grid metadata (MPI_Irecv)\n");
        goto fail_exit;
    }

    err = MPI_Wait(&req, &status);
    if (err != 0) {
        printf("Could not wait for grid metadata (MPI_Wait)\n");
        goto fail_exit;
    }

    grid_t* grid = grid_create(grid_metadata.width, grid_metadata.height,
                               grid_metadata.padding);

    // Create appropriate type (data struct)
    err = create_data_struct(grid_metadata.width, grid_metadata.height,
                             &data_struct);
    if (err != 0) {
        goto fail_exit;
    }

    // Receive data

    err = MPI_Irecv(grid->data, 1, data_struct, 0, heatsim->rank,
                    heatsim->communicator, &req);
    if (err != 0) {
        printf("Could not receive grid data (MPI_Irecv)\n");
        goto fail_exit;
    }

    err = MPI_Wait(&req, &status);
    if (err != 0) {
        printf("Could not wait for grid data (MPI_Wait)\n");
        goto fail_exit;
    }

    return grid;

fail_exit:
    return NULL;
}

enum border_t { BORDER_NORTH, BORDER_WEST, BORDER_SOUTH, BORDER_EAST };

int heatsim_exchange_borders(heatsim_t* heatsim, grid_t* grid) {
    assert(grid->padding == 1);

    int err;
    MPI_Request reqs[8];
    MPI_Status status[8];

    MPI_Datatype horizontal_type, vertical_type;
    // TODO Create types

    // Vertical type <vector>
    err = MPI_Type_vector(grid->height, 1, grid->width_padded, MPI_DOUBLE,
                          &vertical_type);
    if (err != MPI_SUCCESS) {
        printf("Could not create vector vertical_type\n");
        goto fail_exit;
    }

    err = MPI_Type_commit(&vertical_type);
    if (err != MPI_SUCCESS) {
        printf("Could not commit type vertical_type\n");
        goto fail_exit;
    }

    // Horizontal type <contiguous>
    err = MPI_Type_contiguous(grid->width, MPI_DOUBLE, &horizontal_type);
    if (err != MPI_SUCCESS) {
        printf("Could not create vector horizontal_type\n");
        goto fail_exit;
    }

    err = MPI_Type_commit(&horizontal_type);
    if (err != MPI_SUCCESS) {
        printf("Could not commit type horizontal_type\n");
        goto fail_exit;
    }

    /*
     * TODO: Échange les bordures de `grid`, excluant le rembourrage, dans le
     *       rembourrage du voisin de ce rang. Par exemple, soit la `grid`
     *       4x4 suivante,
     *
     *                            +-------------+
     *                            | x x x x x x |
     *                            | x A B C D x |
     *                            | x E F G H x |
     *                            | x I J K L x |
     *                            | x M N O P x |
     *                            | x x x x x x |
     *                            +-------------+
     *
     *       où `x` est le rembourrage (padding = 1). Ce rang devrait envoyer
     *
     *        - la bordure [A B C D] au rang nord,
     *        - la bordure [M N O P] au rang sud,
     *        - la bordure [A E I M] au rang ouest et
     *        - la bordure [D H L P] au rang est.
     *
     *       Ce rang devrait aussi recevoir dans son rembourrage
     *
     *        - la bordure [A B C D] du rang sud,
     *        - la bordure [M N O P] du rang nord,
     *        - la bordure [A E I M] du rang est et
     *        - la bordure [D H L P] du rang ouest.
     *
     *       Après l'échange, le `grid` devrait avoir ces données dans son
     *       rembourrage provenant des voisins:
     *
     *                            +-------------+
     *                            | x m n o p x |
     *                            | d A B C D a |
     *                            | h E F G H e |
     *                            | l I J K L i |
     *                            | p M N O P m |
     *                            | x a b c d x |
     *                            +-------------+
     *
     *       Utilisez `grid_get_cell` pour obtenir un pointeur vers une cellule.
     */

    // Northern border

    err = MPI_Isend(grid_get_cell(grid, 0, grid->height - 1), 1,
                    horizontal_type, heatsim->rank_north_peer, BORDER_NORTH,
                    heatsim->communicator, &reqs[0]);
    if (err != 0) {
        printf("Could not send northern border (MPI_Isend)\n");
        goto fail_exit;
    }

    err = MPI_Irecv(grid_get_cell(grid, 0, -1), 1, horizontal_type,
                    heatsim->rank_south_peer, BORDER_NORTH,
                    heatsim->communicator, &reqs[1]);
    if (err != 0) {
        printf("Could not receive southern border (MPI_Isend)\n");
        goto fail_exit;
    }

    // western border

    err = MPI_Isend(grid_get_cell(grid, 0, 0), 1, vertical_type,
                    heatsim->rank_west_peer, BORDER_WEST, heatsim->communicator,
                    &reqs[2]);
    if (err != 0) {
        printf("Could not send western border (MPI_Isend)\n");
        goto fail_exit;
    }

    err = MPI_Irecv(grid_get_cell(grid, grid->width, 0), 1, vertical_type,
                    heatsim->rank_east_peer, BORDER_WEST, heatsim->communicator,
                    &reqs[3]);
    if (err != 0) {
        printf("Could not receive eastern border (MPI_Isend)\n");
        goto fail_exit;
    }

    // Southern border

    err = MPI_Isend(grid_get_cell(grid, 0, 0), 1, horizontal_type,
                    heatsim->rank_south_peer, BORDER_SOUTH,
                    heatsim->communicator, &reqs[4]);
    if (err != 0) {
        printf("Could not send southern border (MPI_Isend)\n");
        goto fail_exit;
    }

    err = MPI_Irecv(grid_get_cell(grid, 0, grid->height), 1, horizontal_type,
                    heatsim->rank_north_peer, BORDER_SOUTH,
                    heatsim->communicator, &reqs[5]);
    if (err != 0) {
        printf("Could not receive southern border (MPI_Isend)\n");
        goto fail_exit;
    }

    // Eastern border

    err = MPI_Isend(grid_get_cell(grid, grid->width - 1, 0), 1, vertical_type,
                    heatsim->rank_east_peer, BORDER_EAST, heatsim->communicator,
                    &reqs[6]);
    if (err != 0) {
        printf("Could not send western border (MPI_Isend)\n");
        goto fail_exit;
    }

    err = MPI_Irecv(grid_get_cell(grid, -1, 0), 1, vertical_type,
                    heatsim->rank_west_peer, BORDER_EAST, heatsim->communicator,
                    &reqs[7]);
    if (err != 0) {
        printf("Could not receive eastern border (MPI_Isend)\n");
        goto fail_exit;
    }

    // Wait for all send && receive
    err = MPI_Waitall(8, reqs, status);
    if (err != 0) {
        printf("Could not wait for border exchanges (MPI_Waitall)\n");
        goto fail_exit;
    }

    return 0;

fail_exit:
    return -1;
}

int heatsim_send_result(heatsim_t* heatsim, grid_t* grid) {
    assert(grid->padding == 0);

    int err;
    int len = grid->width * grid->height;

    MPI_Request req;
    MPI_Status status;

    err = MPI_Isend(grid->data, len, MPI_DOUBLE, 0, heatsim->rank,
                    heatsim->communicator, &req);
    if (err != 0) {
        printf("Could not send data (MPI_Isend)\n");
        goto fail_exit;
    }

    err = MPI_Wait(&req, &status);
    if (err != 0) {
        printf("Could not wait for grid data send (MPI_Wait)\n");
        goto fail_exit;
    }

    return 0;

fail_exit:
    return -1;
}

int heatsim_receive_results(heatsim_t* heatsim, cart2d_t* cart) {
    /*
     * TODO: Recevoir toutes les `grid` des autres rangs. Aucune `grid`
     *       n'a de rembourage (padding = 0).
     *
     *       Utilisez `cart2d_get_grid` pour obtenir la `grid` à une coordonnée
     *       qui va recevoir le contenue (`data`) d'un autre noeud.
     */

    int err;

    int nb_rank = heatsim->rank_count - 1; // Excluding rank 0

    MPI_Request* req = calloc(nb_rank, sizeof(MPI_Request));
    if (req == NULL) {
        printf("[0] Could not alloc requests\n");
        goto fail_nofree;
    }
    MPI_Status* status = calloc(nb_rank, sizeof(MPI_Status));
    if (status == NULL) {
        printf("[0] Could not alloc statuses\n");
        goto fail_freereq;
    }

    for (int rank = 1; rank < heatsim->rank_count; ++rank) {
        int coords[2];
        // Get peer coords
        err = MPI_Cart_coords(heatsim->communicator, rank, 2, coords);
        if (err != MPI_SUCCESS) {
            printf("Could not get coords of node %d shift\n", rank);
            goto fail_exit;
        }

        grid_t* grid = cart2d_get_grid(cart, coords[0], coords[1]);

        int len = grid->width * grid->height;

        printf("[0] Receiving from %d\n", rank);
        err = MPI_Irecv(grid->data, len, MPI_DOUBLE, rank, rank,
                        heatsim->communicator, &req[rank - 1]);
        if (err != 0) {
            printf("Could not send grid data (MPI_Irecv)\n");
            goto fail_exit;
        }
    }

    // Wait for completion (only one waitall)
    err = MPI_Waitall(nb_rank, req, status);
    if (err != 0) {
        printf("Could not wait for grid data recv (MPI_Waitall)\n");
        goto fail_exit;
    }
    printf("[0] Received all grids\n");

    free(req);
    free(status);

    return 0;

fail_exit:
    free(status);
fail_freereq:
    free(req);
fail_nofree:
    return -1;
}
