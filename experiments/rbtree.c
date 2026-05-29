#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define PAGE_SIZE 4096

void *page_alloc(void) {
    return calloc(1, 4096);
}

void page_free(void *page) {
    free(page);
}

typedef struct Test_Range {
    uintptr_t start;
    size_t len;
} Test_Range;

int test_range_compare(Test_Range a, Test_Range b) {
    if (a.start < b.start) {
        return -1;
    }
    if (a.start > b.start) {
        return 1;
    }
    if (a.len < b.len) {
        return -1;
    }
    if (a.len > b.len) {
        return 1;
    }
    return 0;
}

bool test_range_valid(Test_Range range) {
    if (range.len <= 0) {
        return false;
    }

    if (range.len > UINTPTR_MAX - range.start) {
        return false;
    }

    return true;
}

bool test_range_overlap(Test_Range a, Test_Range b) {
    assert(test_range_valid(a) && test_range_valid(b));

    return a.start < b.start + b.len && b.start < a.start + a.len;
}

void test_range_print(Test_Range range) {
    printf("[0x%" PRIxPTR ", +%zu]", range.start, range.len);
}

typedef enum RB_Direction {
    RB_LEFT,
    RB_RIGHT,
    RB__COUNT,

    RB_COLOR_DIRECTION = RB_LEFT,
    RB_COLOR_NEXT_FREE = RB_RIGHT,
} RB_Direction;

RB_Direction rb_direction_flip(RB_Direction direction) { return direction ^ 1; }

typedef enum RB_Color {
    RBC_BLACK,
    RBC_RED,
} RB_Color;

RB_Color rb_color_flip(RB_Color color) { return color ^ 1; }

typedef struct Node {
    Test_Range range;
    struct Node *children[RB__COUNT];
} Node;

Node node_nil = {
    .children = { &node_nil, &node_nil },
};

Node *node_get(Node *node) {
    return (Node *)(((uintptr_t)node) & ~(uintptr_t)1);
}

bool node_valid(Node *node) {
    return node != NULL && node_get(node) == node;
}

bool node_is_nil(Node *node) {
    assert(node_valid(node));
    return node == &node_nil;
}

#define NODE_PTR_MASK ~(uintptr_t)1

RB_Color node_get_color(Node *node) {
    assert(node_valid(node));
    assert(!node_is_nil(node));
    return (uintptr_t)node->children[RB_COLOR_DIRECTION] & (uintptr_t)1;
}

void node_set_color(Node *node, RB_Color color) {
    assert(node_valid(node));
    assert(!node_is_nil(node));
    node->children[RB_COLOR_DIRECTION] =
        (Node *)(((uintptr_t)node->children[RB_COLOR_DIRECTION] & NODE_PTR_MASK) |
                (uintptr_t)color);
}

Node *node_get_child(Node *node, RB_Direction direction) {
    assert(node_valid(node));
    assert(!node_is_nil(node));
    Node *child = node->children[direction];

    if (direction == RB_COLOR_DIRECTION) {
        child = node_get(child);
    }

    assert(node_valid(child));
    return child;
}

void node_set_child(Node *node, RB_Direction direction, Node *child) {
    assert(node_valid(node));
    assert(node_valid(child));
    assert(!node_is_nil(node));

    if (direction == RB_COLOR_DIRECTION) {
        child = (Node *)((uintptr_t)child | node_get_color(node));
    }

    node->children[direction] = child;
}

bool node_has_child(Node *node, RB_Direction direction) {
    assert(!node_is_nil(node));
    return !node_is_nil(node_get_child(node, direction));
}


bool node_red(Node *node) {
    if (node_is_nil(node)) {
        return false;
    }
    return node_get_color(node) == RBC_RED;
}

bool node_child_red(Node *node, RB_Direction direction) {
    assert(!node_is_nil(node));
    return node_red(node_get_child(node, direction));
}

void node_color_flip(Node *node) {
    assert(!node_is_nil(node));
    node_set_color(node, node_get_color(node) ^ 1);

    Node *left = node_get_child(node, RB_LEFT);
    if (!node_is_nil(left)) {
        node_set_color(left, node_get_color(left) ^ 1);
    }

    Node *right = node_get_child(node, RB_RIGHT);
    if (!node_is_nil(right)) {
        node_set_color(right, node_get_color(right) ^ 1);
    }
}

Node *node_rotate(Node *node, RB_Direction direction) {
    assert(!node_is_nil(node));
    Node *temp = node_get_child(node, rb_direction_flip(direction));
    assert(!node_is_nil(temp));

    node_set_child(node, rb_direction_flip(direction),
            node_get_child(temp, direction));
    node_set_child(temp, direction, node);

    node_set_color(temp, node_get_color(node));
    node_set_color(node, RBC_RED);

    assert(node_valid(temp));
    return temp;
}

Node *node_double_rotate(Node *node, RB_Direction direction) {
    assert(!node_is_nil(node));
    assert(!node_is_nil(node_get_child(node, rb_direction_flip(direction))));

    node_set_child(node, rb_direction_flip(direction),
            node_rotate(node_get_child(node, rb_direction_flip(direction)),
                rb_direction_flip(direction)));

    Node *result = node_rotate(node, direction);
    assert(node_valid(result));
    return result;
}

#define RB_NODES_MAX ((4096 - sizeof(RB_Nodes)) / sizeof(Node))

typedef struct RB_Nodes {
    struct RB_Nodes *next;
    size_t          node_count;
    size_t          _reserved[2];
    Node            nodes[];
} RB_Nodes;

RB_Nodes *rb_nodes_create(void) {
    return page_alloc();
}

typedef struct RB_Tree {
    Node *root;

    Node *free_list;

    RB_Nodes *nodes;
    RB_Nodes *current_node;
} RB_Tree;

bool rb_tree_valid(RB_Tree *tree) {
    return tree != NULL
           && node_valid(tree->root)
           && tree->nodes != NULL
           && tree->current_node != NULL;
}

bool rb_tree_init(RB_Tree *tree) {
    tree->root  = &node_nil;
    RB_Nodes *new_nodes = rb_nodes_create();
    if (new_nodes == NULL) {
        return false;
    }
    tree->nodes = tree->current_node = new_nodes;
    return true;
}

Node *rb_tree_node_make(RB_Tree *tree, Test_Range range, RB_Color color) {
    assert(rb_tree_valid(tree));
    assert(test_range_valid(range));

    Node *node = NULL;

    if (tree->free_list != NULL) {
        node            = tree->free_list;
        tree->free_list = node->children[RB_COLOR_NEXT_FREE];
        *node           = (Node){ 0 };
    } else if (tree->current_node->node_count < RB_NODES_MAX) {
        tree->current_node->node_count += 1;
        node                            = &tree->current_node->nodes[tree->current_node->node_count - 1];
    } else {
        RB_Nodes *new_nodes   = rb_nodes_create();
        if (!new_nodes) {
            return NULL;
        }
        tree->current_node    = tree->current_node->next = new_nodes;
        node                  = &new_nodes->nodes[0];
        new_nodes->node_count = 1;
    }

    if (node == NULL) {
        return NULL;
    }

    assert((((uintptr_t)node) & (sizeof(uintptr_t) - 1)) == 0);
    node->range = range;
    node->children[RB_LEFT] = &node_nil;
    node->children[RB_RIGHT] = &node_nil;

    node_set_color(node, color);

    return node;
}

void rb_tree_node_free(RB_Tree *tree, Node *node, bool recurse) {
    assert(rb_tree_valid(tree));
    assert(node_valid(node));
    if (node_is_nil(node)) {
        return;
    }

    if (recurse) {
        rb_tree_node_free(tree, node_get_child(node, RB_LEFT), true);
        rb_tree_node_free(tree, node_get_child(node, RB_RIGHT), true);
    }

    node->children[RB_COLOR_NEXT_FREE] = tree->free_list;
    tree->free_list                    = node;
}

void rb_destroy(RB_Tree *tree) {
    if (tree == NULL) {
        return;
    }

    if (!rb_tree_valid(tree)) {
        *tree = (RB_Tree){ 0 };
        return;
    }

    RB_Nodes *current  = tree->nodes;
    RB_Nodes *previous = NULL;
    for (;;) {
        if (!current) {
            break;
        }

        previous = current;
        current  = current->next;

        page_free(previous);
    }

    *tree = (RB_Tree){ 0 };
}

void rb_clear(RB_Tree *tree) {
    assert(rb_tree_valid(tree));
    rb_tree_node_free(tree, tree->root, true);
    tree->root = &node_nil;
}

typedef enum RB_Insert_Status {
    RB_INSERT_OK,
    RB_INSERT_OVERLAP,
    RB_INSERT_OOM,
} RB_Insert_Status;

typedef struct RB_Insert_Result {
    Node *node;
    RB_Insert_Status status;
} RB_Insert_Result;

Node *rb_insert_fix_up(Node *node, RB_Direction direction) {
    assert(!node_is_nil(node));

    if (node_child_red(node, direction)) {
        Node *dir_child = node_get_child(node, direction);

        if (node_child_red(node, rb_direction_flip(direction))) {
            if (node_child_red(dir_child, direction) ||
                    node_child_red(dir_child, rb_direction_flip(direction))) {
                node_color_flip(node);
            }
        } else {
            if (node_child_red(dir_child, direction)) {
                node = node_rotate(node, rb_direction_flip(direction));
            } else if (node_child_red(dir_child, rb_direction_flip(direction))) {
                node = node_double_rotate(node, rb_direction_flip(direction));
            }
        }
    }

    assert(node_valid(node));
    return node;
}

RB_Insert_Result rb_insert_helper(RB_Tree *tree, Node *node, Test_Range range) {
    assert(rb_tree_valid(tree));
    assert(node_valid(node));
    assert(test_range_valid(range));
    if (node_is_nil(node)) {
        Node *new_node = rb_tree_node_make(tree, range, RBC_RED);
        if (new_node == NULL) {
            return (RB_Insert_Result) {
                .node   = &node_nil,
                .status = RB_INSERT_OOM,
            };
        }

        RB_Insert_Result result = {
            .node = new_node,
            .status = RB_INSERT_OK,
        };
        assert(node_valid(result.node));
        return result;
    }

    if (test_range_overlap(range, node->range)) {
        RB_Insert_Result result = {
            .node = node,
            .status = RB_INSERT_OVERLAP,
        };
        assert(node_valid(result.node));
        return result;
    }

    int comparison = test_range_compare(range, node->range);
    assert(comparison != 0);
    RB_Direction direction = comparison > 0;

    RB_Insert_Result result =
        rb_insert_helper(tree, node_get_child(node, direction), range);

    if (result.status == RB_INSERT_OK) {
        node_set_child(node, direction, result.node);
        node = rb_insert_fix_up(node, direction);
    }

    result.node = node;
    assert(node_valid(result.node));
    return result;
}

RB_Insert_Status rb_insert(RB_Tree *tree, Test_Range range) {
    assert(rb_tree_valid(tree));
    assert(test_range_valid(range));

    RB_Insert_Result result = rb_insert_helper(tree, tree->root, range);
    tree->root = result.node;

    if (result.status == RB_INSERT_OK) {
        node_set_color(tree->root, RBC_BLACK);
    }

    return result.status;
}

Node *rb_delete_fix_up(Node *node, RB_Direction direction, bool *ok) {
    assert(node_valid(node));
    assert(!node_is_nil(node));
    assert(ok);

    Node *parent = node;
    Node *sibling = node_get_child(node, rb_direction_flip(direction));

    if (node_red(sibling)) {
        node = node_rotate(node, direction);
        sibling = node_get_child(parent, rb_direction_flip(direction));
    }

    if (!node_is_nil(sibling)) {
        if (!node_child_red(sibling, RB_LEFT) &&
                !node_child_red(sibling, RB_RIGHT)) {
            if (node_red(parent)) {
                *ok = true;
            }

            node_set_color(parent, RBC_BLACK);
            node_set_color(sibling, RBC_RED);
        } else {
            RB_Color initcol_parent = node_get_color(parent);
            bool is_red_sibling_reduction = !(node == parent);

            if (node_child_red(sibling, rb_direction_flip(direction))) {
                parent = node_rotate(parent, direction);
            } else {
                parent = node_double_rotate(parent, direction);
            }

            node_set_color(parent, initcol_parent);
            node_set_color(node_get_child(parent, RB_LEFT), RBC_BLACK);
            node_set_color(node_get_child(parent, RB_RIGHT), RBC_BLACK);

            if (is_red_sibling_reduction) {
                node_set_child(node, direction, parent);
            } else {
                node = parent;
            }

            *ok = true;
        }
    }

    assert(node_valid(node));
    return node;
}

Node *rb_delete_helper(RB_Tree *tree, Node *node, Test_Range range, bool *ok) {
    assert(rb_tree_valid(tree));
    assert(test_range_valid(range));
    assert(ok);

    if (node_is_nil(node)) {
        *ok = true;
        assert(node_valid(node));
        return node;
    }
    assert(!node_is_nil(node));

    if (test_range_compare(node->range, range) == 0) {
        if (!node_has_child(node, RB_LEFT) || !node_has_child(node, RB_RIGHT)) {

            Node *temp = &node_nil;
            if (node_has_child(node, RB_LEFT)) {
                temp = node_get_child(node, RB_LEFT);
            }
            if (node_has_child(node, RB_RIGHT)) {
                temp = node_get_child(node, RB_RIGHT);
            }

            if (node_red(node)) {
                *ok = true;
            } else if (node_red(temp)) {
                node_set_color(temp, RBC_BLACK);
                *ok = true;
            }
            rb_tree_node_free(tree, node, false);

            assert(node_valid(temp));
            return temp;
        } else {
            Node *temp = node_get_child(node, RB_LEFT);
            while (node_has_child(temp, RB_RIGHT)) {
                temp = node_get_child(temp, RB_RIGHT);
            }

            node->range = temp->range;
            range = temp->range;
        }
    }

    RB_Direction direction = test_range_compare(range, node->range) > 0;
    node_set_child(node, direction,
            rb_delete_helper(tree, node_get_child(node, direction), range, ok));

    Node *result = *ok ? node : rb_delete_fix_up(node, direction, ok);
    assert(node_valid(result));
    return result;
}

void rb_delete(RB_Tree *tree, Test_Range range) {
    assert(rb_tree_valid(tree));
    assert(test_range_valid(range));

    bool ok = false;
    tree->root = rb_delete_helper(tree, tree->root, range, &ok);
    if (!node_is_nil(tree->root)) {
        node_set_color(tree->root, RBC_BLACK);
    }
}

void rb_print_node(Node *node, const char *prefix, bool is_tail) {
    if (node_is_nil(node)) {
        return;
    }

    printf("%s%s", prefix, is_tail ? "`-- " : "|-- ");
    test_range_print(node->range);
    printf("%c\n", node_get_color(node) == RBC_RED ? 'R' : 'B');

    char next_prefix[256];
    snprintf(next_prefix, sizeof(next_prefix), "%s%s", prefix,
            is_tail ? "    " : "|   ");

    Node *left = node_get_child(node, RB_LEFT);
    Node *right = node_get_child(node, RB_RIGHT);

    if (!node_is_nil(left) && !node_is_nil(right)) {
        rb_print_node(left, next_prefix, false);
        rb_print_node(right, next_prefix, true);
    } else if (!node_is_nil(left)) {
        rb_print_node(left, next_prefix, true);
    } else if (!node_is_nil(right)) {
        rb_print_node(right, next_prefix, true);
    }
}

void rb_print(RB_Tree *tree) {
    if (node_is_nil(tree->root)) {
        printf("(empty)\n");
        return;
    }

    test_range_print(tree->root->range);
    printf("%c\n", node_get_color(tree->root) == RBC_RED ? 'R' : 'B');

    Node *left = node_get_child(tree->root, RB_LEFT);
    Node *right = node_get_child(tree->root, RB_RIGHT);

    if (!node_is_nil(left) && !node_is_nil(right)) {
        rb_print_node(left, "", false);
        rb_print_node(right, "", true);
    } else if (!node_is_nil(left)) {
        rb_print_node(left, "", true);
    } else if (!node_is_nil(right)) {
        rb_print_node(right, "", true);
    }
}

int main(void) {
    RB_Tree tree = {0};
    assert(rb_tree_init(&tree));

    Test_Range ranges[] = {
        {.start = 8, .len = 1}, {.start = 3, .len = 1}, {.start = 10, .len = 1},
        {.start = 1, .len = 1}, {.start = 6, .len = 1}, {.start = 14, .len = 1},
        {.start = 4, .len = 1}, {.start = 7, .len = 1}, {.start = 13, .len = 1},
    };

    printf("Insert ranges:\n");
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i) {
        printf("insert ");
        test_range_print(ranges[i]);
        printf("\n");
        rb_insert(&tree, ranges[i]);
        rb_print(&tree);
    }

    printf("\nTree after inserts:\n");
    rb_print(&tree);

    printf("\nDelete ");
    test_range_print((Test_Range){.start = 1, .len = 1});
    printf(":\n");
    rb_delete(&tree, (Test_Range){.start = 1, .len = 1});
    rb_print(&tree);

    printf("\nDelete ");
    test_range_print((Test_Range){.start = 14, .len = 1});
    printf(":\n");
    rb_delete(&tree, (Test_Range){.start = 14, .len = 1});
    rb_print(&tree);

    rb_destroy(&tree);

    return 0;
}
