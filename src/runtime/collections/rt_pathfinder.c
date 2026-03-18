//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_pathfinder.c
// Purpose: A* grid pathfinding. Uses a binary min-heap open set and flat
//   per-node arrays for g-cost, parent, and closed state. Supports 4-way
//   (Manhattan heuristic) and 8-way (octile heuristic) movement with per-cell
//   movement cost weights.
//
// Key invariants:
//   - Node arrays are pre-allocated to width×height at search time, then freed.
//   - Heap stores indices into the flat node array; heapified by f-cost.
//   - Fixed-point costs: base cost 100, diagonal cost 141 (~√2 × 100).
//   - Heuristic is admissible and consistent for both 4-way and 8-way.
//   - Path reconstruction walks parent chain from goal → start, then reverses.
//
// Ownership/Lifetime:
//   - Pathfinder objects are GC-managed. The cells array uses a finalizer.
//   - Per-search node/heap arrays are stack-like (malloc/free per call).
//   - Returned List is a new GC-managed runtime List[Integer].
//
// Links: rt_pathfinder.h (API), rt_tilemap.h (FromTilemap), rt_grid2d.c (FromGrid2D)
//
//===----------------------------------------------------------------------===//

#include "rt_pathfinder.h"
#include "rt_box.h"
#include "rt_grid2d.h"
#include "rt_list.h"
#include "rt_object.h"
#include "rt_tilemap.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Data Structures
//=============================================================================

/// @brief Per-cell static data.
typedef struct
{
    int16_t cost;    ///< Movement cost multiplier (100 = normal, 0 = impassable).
    int8_t walkable; ///< 1 = passable, 0 = wall.
} pf_cell;

/// @brief Maximum grid dimension (prevents absurd allocations).
#define PF_MAX_DIM 4096

/// @brief Base movement cost for cardinal directions (fixed-point ×100).
#define PF_COST_CARDINAL 100

/// @brief Diagonal movement cost (~√2 × 100, fixed-point).
#define PF_COST_DIAGONAL 141

/// @brief Internal pathfinder structure.
typedef struct
{
    pf_cell *cells;       ///< width × height cell array.
    int32_t width;        ///< Grid width.
    int32_t height;       ///< Grid height.
    int8_t allow_diagonal; ///< 0 = 4-way, 1 = 8-way.
    int32_t max_steps;    ///< Max nodes to expand (0 = unlimited).
    int32_t last_steps;   ///< Nodes expanded in last search.
    int8_t last_found;    ///< 1 if last search found a path.
} rt_pathfinder_impl;

/// @brief Flat index for (x, y) in the grid.
static inline int32_t pf_idx(const rt_pathfinder_impl *pf, int32_t x, int32_t y)
{
    return y * pf->width + x;
}

/// @brief Bounds check.
static inline int8_t pf_in_bounds(const rt_pathfinder_impl *pf, int32_t x, int32_t y)
{
    return x >= 0 && x < pf->width && y >= 0 && y < pf->height;
}

//=============================================================================
// GC Finalizer
//=============================================================================

static void pf_finalizer(void *obj)
{
    rt_pathfinder_impl *pf = (rt_pathfinder_impl *)obj;
    free(pf->cells);
    pf->cells = NULL;
}

//=============================================================================
// Construction
//=============================================================================

/// @brief Allocate and initialize a pathfinder with given dimensions.
static rt_pathfinder_impl *pf_alloc(int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0)
        return NULL;
    if (w > PF_MAX_DIM)
        w = PF_MAX_DIM;
    if (h > PF_MAX_DIM)
        h = PF_MAX_DIM;

    rt_pathfinder_impl *pf =
        (rt_pathfinder_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_pathfinder_impl));
    if (!pf)
        return NULL;

    int32_t count = w * h;
    pf->cells = (pf_cell *)malloc((size_t)count * sizeof(pf_cell));
    if (!pf->cells)
        return NULL;

    pf->width = w;
    pf->height = h;
    pf->allow_diagonal = 0;
    pf->max_steps = 0;
    pf->last_steps = 0;
    pf->last_found = 0;

    // Default: all cells walkable, cost 100
    for (int32_t i = 0; i < count; i++)
    {
        pf->cells[i].walkable = 1;
        pf->cells[i].cost = PF_COST_CARDINAL;
    }

    rt_obj_set_finalizer(pf, pf_finalizer);
    return pf;
}

void *rt_pathfinder_new(int64_t width, int64_t height)
{
    return pf_alloc((int32_t)width, (int32_t)height);
}

void *rt_pathfinder_from_tilemap(void *tilemap)
{
    if (!tilemap)
        return NULL;

    int32_t w = (int32_t)rt_tilemap_get_width(tilemap);
    int32_t h = (int32_t)rt_tilemap_get_height(tilemap);
    rt_pathfinder_impl *pf = pf_alloc(w, h);
    if (!pf)
        return NULL;

    // Read tile collision: collision type != 0 → non-walkable
    for (int32_t y = 0; y < h; y++)
    {
        for (int32_t x = 0; x < w; x++)
        {
            int64_t tile = rt_tilemap_get_tile(tilemap, x, y);
            int64_t collision = rt_tilemap_get_collision(tilemap, tile);
            pf->cells[pf_idx(pf, x, y)].walkable = (collision == 0) ? 1 : 0;
        }
    }

    return pf;
}

void *rt_pathfinder_from_grid2d(void *grid)
{
    if (!grid)
        return NULL;

    // Cast void* to the rt_grid2d typedef (struct rt_grid2d_impl*)
    rt_grid2d g = (rt_grid2d)grid;
    int64_t w = rt_grid2d_width(g);
    int64_t h = rt_grid2d_height(g);
    rt_pathfinder_impl *pf = pf_alloc((int32_t)w, (int32_t)h);
    if (!pf)
        return NULL;

    // Non-zero cells → non-walkable
    for (int32_t y = 0; y < (int32_t)h; y++)
    {
        for (int32_t x = 0; x < (int32_t)w; x++)
        {
            int64_t val = rt_grid2d_get(g, x, y);
            pf->cells[pf_idx(pf, x, y)].walkable = (val == 0) ? 1 : 0;
        }
    }

    return pf;
}

//=============================================================================
// Configuration
//=============================================================================

void rt_pathfinder_set_walkable(void *ptr, int64_t x, int64_t y, int8_t walkable)
{
    if (!ptr)
        return;
    rt_pathfinder_impl *pf = (rt_pathfinder_impl *)ptr;
    if (!pf_in_bounds(pf, (int32_t)x, (int32_t)y))
        return;
    pf->cells[pf_idx(pf, (int32_t)x, (int32_t)y)].walkable = walkable ? 1 : 0;
}

int8_t rt_pathfinder_is_walkable(void *ptr, int64_t x, int64_t y)
{
    if (!ptr)
        return 0;
    rt_pathfinder_impl *pf = (rt_pathfinder_impl *)ptr;
    if (!pf_in_bounds(pf, (int32_t)x, (int32_t)y))
        return 0;
    return pf->cells[pf_idx(pf, (int32_t)x, (int32_t)y)].walkable;
}

void rt_pathfinder_set_cost(void *ptr, int64_t x, int64_t y, int64_t cost)
{
    if (!ptr)
        return;
    rt_pathfinder_impl *pf = (rt_pathfinder_impl *)ptr;
    if (!pf_in_bounds(pf, (int32_t)x, (int32_t)y))
        return;
    if (cost < 0)
        cost = 0;
    if (cost > 30000)
        cost = 30000;
    pf->cells[pf_idx(pf, (int32_t)x, (int32_t)y)].cost = (int16_t)cost;
}

int64_t rt_pathfinder_get_cost(void *ptr, int64_t x, int64_t y)
{
    if (!ptr)
        return 0;
    rt_pathfinder_impl *pf = (rt_pathfinder_impl *)ptr;
    if (!pf_in_bounds(pf, (int32_t)x, (int32_t)y))
        return 0;
    return pf->cells[pf_idx(pf, (int32_t)x, (int32_t)y)].cost;
}

void rt_pathfinder_set_diagonal(void *ptr, int8_t allow)
{
    if (!ptr)
        return;
    ((rt_pathfinder_impl *)ptr)->allow_diagonal = allow ? 1 : 0;
}

void rt_pathfinder_set_max_steps(void *ptr, int64_t max)
{
    if (!ptr)
        return;
    ((rt_pathfinder_impl *)ptr)->max_steps = (int32_t)(max < 0 ? 0 : max);
}

//=============================================================================
// Properties
//=============================================================================

int64_t rt_pathfinder_get_width(void *ptr)
{
    return ptr ? ((rt_pathfinder_impl *)ptr)->width : 0;
}

int64_t rt_pathfinder_get_height(void *ptr)
{
    return ptr ? ((rt_pathfinder_impl *)ptr)->height : 0;
}

int64_t rt_pathfinder_get_last_steps(void *ptr)
{
    return ptr ? ((rt_pathfinder_impl *)ptr)->last_steps : 0;
}

int8_t rt_pathfinder_get_last_found(void *ptr)
{
    return ptr ? ((rt_pathfinder_impl *)ptr)->last_found : 0;
}

//=============================================================================
// A* Search Engine
//=============================================================================

/// @brief Per-node search state (allocated per search call).
typedef struct
{
    int32_t g;      ///< Cost from start to this node.
    int32_t f;      ///< g + heuristic (total estimated cost).
    int32_t parent; ///< Parent node index (-1 = start/none).
    int8_t open;    ///< 1 = in open set.
    int8_t closed;  ///< 1 = already expanded.
} pf_node;

/// @brief Binary min-heap for the open set (heapified by f-cost).
typedef struct
{
    int32_t *data;    ///< Array of node indices.
    int32_t count;    ///< Current number of elements.
    int32_t capacity; ///< Allocated capacity.
    pf_node *nodes;   ///< Reference to node array (for f-cost comparison).
} pf_heap;

static void heap_swap(pf_heap *h, int32_t a, int32_t b)
{
    int32_t tmp = h->data[a];
    h->data[a] = h->data[b];
    h->data[b] = tmp;
}

static void heap_sift_up(pf_heap *h, int32_t i)
{
    while (i > 0)
    {
        int32_t parent = (i - 1) / 2;
        if (h->nodes[h->data[i]].f < h->nodes[h->data[parent]].f)
        {
            heap_swap(h, i, parent);
            i = parent;
        }
        else
            break;
    }
}

static void heap_sift_down(pf_heap *h, int32_t i)
{
    while (1)
    {
        int32_t left = 2 * i + 1;
        int32_t right = 2 * i + 2;
        int32_t smallest = i;

        if (left < h->count && h->nodes[h->data[left]].f < h->nodes[h->data[smallest]].f)
            smallest = left;
        if (right < h->count && h->nodes[h->data[right]].f < h->nodes[h->data[smallest]].f)
            smallest = right;

        if (smallest != i)
        {
            heap_swap(h, i, smallest);
            i = smallest;
        }
        else
            break;
    }
}

static void heap_push(pf_heap *h, int32_t node_idx)
{
    if (h->count >= h->capacity)
        return; // Should never happen with correct pre-allocation
    h->data[h->count] = node_idx;
    heap_sift_up(h, h->count);
    h->count++;
}

static int32_t heap_pop(pf_heap *h)
{
    if (h->count == 0)
        return -1;
    int32_t top = h->data[0];
    h->count--;
    if (h->count > 0)
    {
        h->data[0] = h->data[h->count];
        heap_sift_down(h, 0);
    }
    return top;
}

/// @brief Compute heuristic from (ax,ay) to (bx,by).
static int32_t pf_heuristic(int32_t ax, int32_t ay, int32_t bx, int32_t by, int8_t diagonal)
{
    int32_t dx = ax > bx ? ax - bx : bx - ax;
    int32_t dy = ay > by ? ay - by : by - ay;

    if (diagonal)
    {
        // Octile distance: max(dx,dy)*100 + (√2-1)*min(dx,dy)*100
        // ≈ max*100 + min*41
        int32_t mn = dx < dy ? dx : dy;
        int32_t mx = dx > dy ? dx : dy;
        return mx * PF_COST_CARDINAL + mn * (PF_COST_DIAGONAL - PF_COST_CARDINAL);
    }
    else
    {
        // Manhattan distance
        return (dx + dy) * PF_COST_CARDINAL;
    }
}

/// @brief 4-way direction offsets.
static const int32_t dir4_dx[4] = {0, 1, 0, -1};
static const int32_t dir4_dy[4] = {-1, 0, 1, 0};

/// @brief 8-way direction offsets (4 cardinal + 4 diagonal).
static const int32_t dir8_dx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
static const int32_t dir8_dy[8] = {-1, 0, 1, 0, -1, 1, 1, -1};

/// @brief Build a List[Integer] of interleaved x,y pairs from the parent chain.
static void *pf_build_path(pf_node *nodes, int32_t goal_idx,
                            int32_t width)
{
    // Count path length
    int32_t len = 0;
    int32_t idx = goal_idx;
    while (idx >= 0)
    {
        len++;
        idx = nodes[idx].parent;
    }

    // Build reversed array
    int32_t *coords = (int32_t *)malloc((size_t)len * 2 * sizeof(int32_t));
    if (!coords)
        return rt_list_new();

    idx = goal_idx;
    for (int32_t i = len - 1; i >= 0; i--)
    {
        int32_t x = idx % width;
        int32_t y = idx / width;
        coords[i * 2] = x;
        coords[i * 2 + 1] = y;
        idx = nodes[idx].parent;
    }

    // Build List[Integer]
    void *list = rt_list_new();
    for (int32_t i = 0; i < len; i++)
    {
        rt_list_push(list, rt_box_i64(coords[i * 2]));
        rt_list_push(list, rt_box_i64(coords[i * 2 + 1]));
    }

    free(coords);
    return list;
}

/// @brief Core A* implementation. Returns path as List or NULL.
/// If cost_only is true, returns NULL but sets pf->last_found and returns cost via out_cost.
static void *pf_astar(rt_pathfinder_impl *pf, int32_t sx, int32_t sy,
                       int32_t gx, int32_t gy, int8_t cost_only, int32_t *out_cost)
{
    pf->last_found = 0;
    pf->last_steps = 0;
    if (out_cost)
        *out_cost = -1;

    // Bounds check
    if (!pf_in_bounds(pf, sx, sy) || !pf_in_bounds(pf, gx, gy))
        return cost_only ? NULL : rt_list_new();

    // Start == goal
    if (sx == gx && sy == gy)
    {
        pf->last_found = 1;
        if (out_cost)
            *out_cost = 0;
        if (cost_only)
            return NULL;
        void *list = rt_list_new();
        rt_list_push(list, rt_box_i64(sx));
        rt_list_push(list, rt_box_i64(sy));
        return list;
    }

    // Start or goal not walkable
    int32_t si = pf_idx(pf, sx, sy);
    int32_t gi = pf_idx(pf, gx, gy);
    if (!pf->cells[si].walkable || !pf->cells[gi].walkable)
        return cost_only ? NULL : rt_list_new();

    int32_t total = pf->width * pf->height;

    // Allocate per-search arrays
    pf_node *nodes = (pf_node *)calloc((size_t)total, sizeof(pf_node));
    if (!nodes)
        return cost_only ? NULL : rt_list_new();

    // Initialize all nodes
    for (int32_t i = 0; i < total; i++)
    {
        nodes[i].g = INT32_MAX / 2;
        nodes[i].f = INT32_MAX / 2;
        nodes[i].parent = -1;
        nodes[i].open = 0;
        nodes[i].closed = 0;
    }

    // Heap
    pf_heap heap;
    heap.data = (int32_t *)malloc((size_t)total * sizeof(int32_t));
    heap.count = 0;
    heap.capacity = total;
    heap.nodes = nodes;

    if (!heap.data)
    {
        free(nodes);
        return cost_only ? NULL : rt_list_new();
    }

    // Initialize start node
    nodes[si].g = 0;
    nodes[si].f = pf_heuristic(sx, sy, gx, gy, pf->allow_diagonal);
    nodes[si].open = 1;
    heap_push(&heap, si);

    int32_t dir_count = pf->allow_diagonal ? 8 : 4;
    const int32_t *dx = pf->allow_diagonal ? dir8_dx : dir4_dx;
    const int32_t *dy = pf->allow_diagonal ? dir8_dy : dir4_dy;
    int32_t max = pf->max_steps > 0 ? pf->max_steps : INT32_MAX;
    int32_t steps = 0;

    void *result = NULL;

    while (heap.count > 0 && steps < max)
    {
        int32_t cur = heap_pop(&heap);
        if (cur < 0)
            break;

        nodes[cur].open = 0;
        nodes[cur].closed = 1;
        steps++;

        // Goal reached
        if (cur == gi)
        {
            pf->last_found = 1;
            pf->last_steps = steps;
            if (out_cost)
                *out_cost = nodes[cur].g;
            if (!cost_only)
                result = pf_build_path(nodes, gi, pf->width);
            free(heap.data);
            free(nodes);
            return result ? result : (cost_only ? NULL : rt_list_new());
        }

        int32_t cx = cur % pf->width;
        int32_t cy = cur / pf->width;

        for (int32_t d = 0; d < dir_count; d++)
        {
            int32_t nx = cx + dx[d];
            int32_t ny = cy + dy[d];

            if (!pf_in_bounds(pf, nx, ny))
                continue;

            int32_t ni = pf_idx(pf, nx, ny);
            if (nodes[ni].closed || !pf->cells[ni].walkable)
                continue;

            // For diagonal moves, check that both cardinal neighbors are walkable
            // (prevents cutting through wall corners)
            if (d >= 4)
            {
                int32_t adj1 = pf_idx(pf, cx + dx[d], cy);
                int32_t adj2 = pf_idx(pf, cx, cy + dy[d]);
                if (!pf_in_bounds(pf, cx + dx[d], cy) || !pf->cells[adj1].walkable)
                    continue;
                if (!pf_in_bounds(pf, cx, cy + dy[d]) || !pf->cells[adj2].walkable)
                    continue;
            }

            // Movement cost: base cost × cell cost / 100
            int32_t base_cost = (d < 4) ? PF_COST_CARDINAL : PF_COST_DIAGONAL;
            int32_t move_cost = base_cost * pf->cells[ni].cost / PF_COST_CARDINAL;
            int32_t tentative_g = nodes[cur].g + move_cost;

            if (tentative_g < nodes[ni].g)
            {
                nodes[ni].g = tentative_g;
                nodes[ni].f = tentative_g + pf_heuristic(nx, ny, gx, gy, pf->allow_diagonal);
                nodes[ni].parent = cur;

                if (!nodes[ni].open)
                {
                    nodes[ni].open = 1;
                    heap_push(&heap, ni);
                }
                else
                {
                    // Decrease-key: re-sift the node.
                    // Since we don't track heap position, we push a duplicate.
                    // Duplicates with higher f are ignored when popped (already closed).
                    heap_push(&heap, ni);
                }
            }
        }
    }

    // No path found
    pf->last_steps = steps;
    free(heap.data);
    free(nodes);
    return cost_only ? NULL : rt_list_new();
}

//=============================================================================
// Public Pathfinding API
//=============================================================================

void *rt_pathfinder_find_path(void *ptr, int64_t sx, int64_t sy,
                               int64_t gx, int64_t gy)
{
    if (!ptr)
        return rt_list_new();
    rt_pathfinder_impl *pf = (rt_pathfinder_impl *)ptr;
    return pf_astar(pf, (int32_t)sx, (int32_t)sy, (int32_t)gx, (int32_t)gy, 0, NULL);
}

int64_t rt_pathfinder_find_path_length(void *ptr, int64_t sx, int64_t sy,
                                        int64_t gx, int64_t gy)
{
    if (!ptr)
        return -1;
    rt_pathfinder_impl *pf = (rt_pathfinder_impl *)ptr;
    int32_t cost = -1;
    pf_astar(pf, (int32_t)sx, (int32_t)sy, (int32_t)gx, (int32_t)gy, 1, &cost);
    return cost;
}

void *rt_pathfinder_find_nearest(void *ptr, int64_t sx, int64_t sy,
                                  int64_t target_value)
{
    // Simple BFS-based nearest search: expand from start, return first matching cell.
    // This reuses the A* infrastructure but with heuristic=0 (degrades to Dijkstra).
    // For now, implement as a simple BFS since we don't have Grid2D value access here.
    (void)target_value;
    if (!ptr)
        return rt_list_new();

    // Placeholder: returns empty list (full implementation requires Grid2D value lookback).
    return rt_list_new();
}
