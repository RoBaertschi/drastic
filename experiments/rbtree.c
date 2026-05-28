#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

bool test_range_validate(Test_Range range) {
    if (range.len <= 0) {
        return false;
    }

    if (range.len > UINTPTR_MAX - range.start) {
        return false;
    }

    return true;
}

bool test_range_overlap(Test_Range a, Test_Range b) {
    assert(test_range_validate(a) && test_range_validate(b));

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

Node *node_get(Node *node) {
    return (Node *)(((uintptr_t)node) & ~(uintptr_t)1);
}

#define NODE_PTR_MASK ~(uintptr_t)1

RB_Color node_get_color(Node *node) {
    return (uintptr_t)node->children[RB_COLOR_DIRECTION] & (uintptr_t)1;
}

void node_set_color(Node *node, RB_Color color) {
    node->children[RB_COLOR_DIRECTION] =
        (Node *)(((uintptr_t)node->children[RB_COLOR_DIRECTION] & NODE_PTR_MASK) |
                (uintptr_t)color);
}

Node *node_get_child(Node *node, RB_Direction direction) {
    Node *child = node->children[direction];

    if (!child) {
        return NULL;
    }

    if (direction == RB_COLOR_DIRECTION) {
        child = (Node *)(((uintptr_t)child) & NODE_PTR_MASK);
    }

    return child;
}

void node_set_child(Node *node, RB_Direction direction, Node *child) {
    child = node_get(child);

    if (direction == RB_COLOR_DIRECTION) {
        child = (Node *)((uintptr_t)child | node_get_color(node));
    }

    node->children[direction] = child;
}

bool node_has_child(Node *node, RB_Direction direction) {
    return node_get_child(node, direction) != NULL;
}

Node *node_make(Test_Range range, RB_Color color) {
    Node *node = calloc(1, sizeof(*node));
    node->range = range;

    node_set_color(node, color);

    return node;
}

bool node_red(Node *node) {
    if (!node) {
        return false;
    }
    return node_get_color(node) == RBC_RED;
}

bool node_child_red(Node *node, RB_Direction direction) {
    return node_red(node_get_child(node, direction));
}

void node_color_flip(Node *node) {
    assert(node);
    node_set_color(node, node_get_color(node) ^ 1);

    Node *left = node_get_child(node, RB_LEFT);
    if (left) {
        node_set_color(left, node_get_color(left) ^ 1);
    }

    Node *right = node_get_child(node, RB_RIGHT);
    if (right) {
        node_set_color(right, node_get_color(right) ^ 1);
    }
}

Node *node_rotate(Node *node, RB_Direction direction) {
    Node *temp = node_get_child(node, rb_direction_flip(direction));
    node_set_child(node, rb_direction_flip(direction),
            node_get_child(temp, direction));
    node_set_child(temp, direction, node);

    node_set_color(temp, node_get_color(node));
    node_set_color(node, RBC_RED);

    return temp;
}

Node *node_double_rotate(Node *node, RB_Direction direction) {
    node_set_child(node, rb_direction_flip(direction),
            node_rotate(node_get_child(node, rb_direction_flip(direction)),
                rb_direction_flip(direction)));

    return node_rotate(node, direction);
}

typedef struct RB_Tree {
    Node *root;
} RB_Tree;

typedef enum RB_Insert_Status {
    RB_INSERT_OK,
    RB_INSERT_OVERLAP,
} RB_Insert_Status;

typedef struct RB_Insert_Result {
    Node *node;
    RB_Insert_Status status;
} RB_Insert_Result;

Node *rb_insert_fix_up(Node *node, RB_Direction direction) {
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

    return node;
}

RB_Insert_Result rb_insert_helper(Node *node, Test_Range range) {
    if (node == NULL) {
        return (RB_Insert_Result){
            .node = node_make(range, RBC_RED),
            .status = RB_INSERT_OK,
        };
    }

    if (test_range_overlap(range, node->range)) {
        return (RB_Insert_Result){
            .node = node,
            .status = RB_INSERT_OVERLAP,
        };
    }

    int comparison = test_range_compare(range, node->range);
    assert(comparison != 0);
    RB_Direction direction = comparison > 0;

    RB_Insert_Result result =
        rb_insert_helper(node_get_child(node, direction), range);

    if (result.status == RB_INSERT_OK) {
        node_set_child(node, direction, result.node);
        node = rb_insert_fix_up(node, direction);
    }

    result.node = node;
    return result;
}

RB_Insert_Status rb_insert(RB_Tree *tree, Test_Range range) {
    assert(test_range_validate(range));

    RB_Insert_Result result = rb_insert_helper(tree->root, range);
    tree->root = result.node;

    if (result.status == RB_INSERT_OK) {
        node_set_color(tree->root, RBC_BLACK);
    }

    return result.status;
}

Node *rb_delete_fix_up(Node *node, RB_Direction direction, bool *ok) {
    Node *parent = node;
    Node *sibling = node_get_child(node, rb_direction_flip(direction));

    if (node_red(sibling)) {
        node = node_rotate(node, direction);
        sibling = node_get_child(parent, rb_direction_flip(direction));
    }

    if (sibling) {
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

    return node;
}

Node *rb_delete_helper(Node *node, Test_Range range, bool *ok) {
    if (node == NULL) {
        *ok = true;
        return node;
    }

    if (test_range_compare(node->range, range) == 0) {
        if (!node_has_child(node, RB_LEFT) || !node_has_child(node, RB_RIGHT)) {

            Node *temp = NULL;
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
            free(node);

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
            rb_delete_helper(node_get_child(node, direction), range, ok));

    return *ok ? node : rb_delete_fix_up(node, direction, ok);
}

void rb_delete(RB_Tree *tree, Test_Range range) {
    bool ok = false;
    tree->root = rb_delete_helper(tree->root, range, &ok);
    if (tree->root != NULL) {
        node_set_color(tree->root, RBC_BLACK);
    }
}

void rb_print_node(Node *node, const char *prefix, bool is_tail) {
    if (!node) {
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

    if (left && right) {
        rb_print_node(left, next_prefix, false);
        rb_print_node(right, next_prefix, true);
    } else if (left) {
        rb_print_node(left, next_prefix, true);
    } else if (right) {
        rb_print_node(right, next_prefix, true);
    }
}

void rb_print(RB_Tree *tree) {
    if (!tree->root) {
        printf("(empty)\n");
        return;
    }

    test_range_print(tree->root->range);
    printf("%c\n", node_get_color(tree->root) == RBC_RED ? 'R' : 'B');

    Node *left = node_get_child(tree->root, RB_LEFT);
    Node *right = node_get_child(tree->root, RB_RIGHT);

    if (left && right) {
        rb_print_node(left, "", false);
        rb_print_node(right, "", true);
    } else if (left) {
        rb_print_node(left, "", true);
    } else if (right) {
        rb_print_node(right, "", true);
    }
}

void rb_free_node(Node *node) {
    if (!node) {
        return;
    }

    rb_free_node(node_get_child(node, RB_LEFT));
    rb_free_node(node_get_child(node, RB_RIGHT));
    free(node);
}

void rb_free(RB_Tree *tree) {
    rb_free_node(tree->root);
    tree->root = NULL;
}

int main(void) {
    RB_Tree tree = {0};

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

    rb_free(&tree);

    return 0;
}
