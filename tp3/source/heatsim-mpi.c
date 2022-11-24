#include <assert.h>
#include <stddef.h>

#include "heatsim.h"
#include "log.h"

static MPI_Datatype grid_data_type;

MPI_Datatype create_grid_data_type() {
    int err;
    int lengths[1] = {1};
    MPI_Aint displacement[1] = {0};
    MPI_Datatype types[1] = { MPI_DOUBLE };
    
    err = MPI_Type_create_struct(1, lengths, displacement, types, &grid_data_type);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Type_create_struct failed");
        return -1;
    }
    err = MPI_Type_commit(&grid_data_type);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Type_commit failed");
        return -1;
    }
    return grid_data_type;
}

int heatsim_init(heatsim_t* heatsim, unsigned int dim_x, unsigned int dim_y) {
    /*
     * TODO: Initialiser tous les membres de la structure `heatsim`.
     *       Le communicateur doit être périodique. Le communicateur
     *       cartésien est périodique en X et Y.
     */

    int err;

    err = MPI_Comm_size(MPI_COMM_WORLD, &heatsim->rank_count);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Comm_size failed");
        goto fail_exit;
    }

    err = MPI_Comm_rank(MPI_COMM_WORLD, &heatsim->rank);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Comm_rank failed");
        goto fail_exit;
    }

    // Initialiser le communicateur cartésien
    int dims[2] = {dim_x, dim_y};
    int periods[2] = {1, 1};

    err = MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, 0, &heatsim->communicator);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Cart_create failed");
        goto fail_exit;
    }

    // Initialiser les ranks
    err = MPI_Cart_shift(heatsim->communicator, 0, 1, &heatsim->rank_west_peer, &heatsim->rank_east_peer);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Cart_shift W/E failed");
        goto fail_exit;
    }

    err = MPI_Cart_shift(heatsim->communicator, 1, 1, &heatsim->rank_south_peer, &heatsim->rank_north_peer);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Cart_shift S/N failed");
        goto fail_exit;
    }

    err = MPI_Cart_coords(heatsim->communicator, heatsim->rank, 2, heatsim->coordinates);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Cart_coords failed");
        goto fail_exit;
    }

    create_grid_data_type();

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

    for (int dest = 1; dest < heatsim->rank_count; dest++) {
        int coords[2];
        err = MPI_Cart_coords(heatsim->communicator, dest, 2, coords);
        if (err != MPI_SUCCESS) {
            LOG_ERROR("MPI_Cart_coords failed");
            goto fail_exit;
        }

        grid_t* grid = cart2d_get_grid(cart, coords[0], coords[1]);

        unsigned buffer[3] = {grid->width, grid->height, grid->padding};
        err = MPI_Send(buffer, 3, MPI_UNSIGNED, dest, dest, MPI_COMM_WORLD);
        if (err != MPI_SUCCESS) {
            LOG_ERROR("MPI_Send width/height/padding failed");
            goto fail_exit;
        }

        err = MPI_Send(grid->data, grid->width*grid->height, grid_data_type, dest, dest, MPI_COMM_WORLD);
        if (err != MPI_SUCCESS) {
            LOG_ERROR("MPI_Send grid_data_type failed");
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
    MPI_Status status;

    unsigned buffer[3];
    err = MPI_Recv(buffer, 3, MPI_UNSIGNED, 0, heatsim->rank, MPI_COMM_WORLD, &status);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Recv width/height/padding failed");
        goto fail_exit;
    }

    grid_t* grid = grid_create(buffer[0], buffer[1], buffer[2]);
    
    err = MPI_Recv(grid->data, grid->width*grid->height, grid_data_type, 0, heatsim->rank, MPI_COMM_WORLD, &status);  
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Recv grid_data_type failed");
        goto fail_exit;
    }

    return grid;

fail_exit:
    return NULL;
}

int heatsim_exchange_borders(heatsim_t* heatsim, grid_t* grid) {
    assert(grid->padding == 1);

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

    MPI_Request request[2];
    int err;

    // Send north and receive south
    err = MPI_Isend(grid_get_cell(grid, 0, 0), grid->width, MPI_DOUBLE, heatsim->rank_south_peer, heatsim->rank, heatsim->communicator, &request[0]);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_ISend north failed");
        goto fail_exit;
    }

    err = MPI_Irecv(grid_get_cell(grid, 0, grid->height), grid->width, MPI_DOUBLE, heatsim->rank_north_peer, heatsim->rank_north_peer, heatsim->communicator, &request[1]);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Irecv south failed");
        goto fail_exit;
    }

    err = MPI_Wait(&request[0], MPI_STATUS_IGNORE);
    err = MPI_Wait(&request[1], MPI_STATUS_IGNORE);

    // Send south and receive north
    err = MPI_Isend(grid_get_cell(grid, 0, grid->height - 1), grid->width, MPI_DOUBLE, heatsim->rank_north_peer, heatsim->rank, heatsim->communicator, &request[0]);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_ISend south failed");
        goto fail_exit;
    }

    err = MPI_Irecv(grid_get_cell(grid, 0, -1), grid->width, MPI_DOUBLE, heatsim->rank_south_peer, heatsim->rank_south_peer, heatsim->communicator, &request[1]);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Irecv north failed");
        goto fail_exit;
    }

    err = MPI_Wait(&request[0], MPI_STATUS_IGNORE);
    err = MPI_Wait(&request[1], MPI_STATUS_IGNORE);

    // Vector type for east and west
    MPI_Datatype border_vector;
    MPI_Type_vector(grid->height, 1, grid->width_padded, MPI_DOUBLE, &border_vector);
    MPI_Type_commit(&border_vector);

    // Send west and receive east
    err = MPI_Isend(grid_get_cell(grid, 0, 0), 1, border_vector, heatsim->rank_west_peer, heatsim->rank, heatsim->communicator, &request[0]);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_ISend west failed");
        goto fail_exit;
    }

    err = MPI_Irecv(grid_get_cell(grid, grid->width, 0), 1, border_vector, heatsim->rank_east_peer, heatsim->rank_east_peer, heatsim->communicator, &request[1]);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Irecv east failed");
        goto fail_exit;
    }

    err = MPI_Wait(&request[0], MPI_STATUS_IGNORE);
    err = MPI_Wait(&request[1], MPI_STATUS_IGNORE);

    // Send east and receive west
    err = MPI_Isend(grid_get_cell(grid, grid->width-1, 0), 1, border_vector, heatsim->rank_east_peer, heatsim->rank, heatsim->communicator, &request[0]);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_ISend east failed");
        goto fail_exit;
    }

    err = MPI_Irecv(grid_get_cell(grid, -1, 0), 1, border_vector, heatsim->rank_west_peer, heatsim->rank_west_peer, heatsim->communicator, &request[1]);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Irecv west failed");
        goto fail_exit;
    }

    err = MPI_Wait(&request[0], MPI_STATUS_IGNORE);
    err = MPI_Wait(&request[1], MPI_STATUS_IGNORE);

    return 0;

fail_exit:
    return -1;
}

int heatsim_send_result(heatsim_t* heatsim, grid_t* grid) {
    assert(grid->padding == 0);

    /*
     * TODO: Envoyer les données (`data`) du `grid` résultant au rang 0. Le
     *       `grid` n'a aucun rembourage (padding = 0);
     */
    MPI_Request request;
    int err;
    err = MPI_Isend(grid->data, grid->height * grid->width, grid_data_type, 0, heatsim->rank, MPI_COMM_WORLD, &request);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Isend to rank 0 failed");
        goto fail_exit;
    }

    err = MPI_Wait(&request, MPI_STATUS_IGNORE);
    if (err != MPI_SUCCESS) {
        LOG_ERROR("MPI_Wait failed");
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
    
    MPI_Request request;
    int err;
    for(int origin = 1; origin < heatsim->rank_count; origin++) {
        int coords[2];
        err = MPI_Cart_coords(heatsim->communicator, origin, 2, coords);

        grid_t* grid = cart2d_get_grid(cart, coords[0], coords[1]);
        err = MPI_Irecv(grid->data, grid->width * grid->height, grid_data_type, origin, origin, MPI_COMM_WORLD, &request);
        if (err != MPI_SUCCESS) {
            LOG_ERROR("MPI_Irecv failed");
            goto fail_exit;
        }

        err = MPI_Wait(&request, MPI_STATUS_IGNORE);
    }

    return 0;

fail_exit:
    return -1;
}
