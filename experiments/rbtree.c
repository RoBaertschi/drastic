#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum RB_Direction {
    RB_LEFT,
    RB_RIGHT,
    RB__COUNT,

    RB_COLOR_DIRECTION = RB_LEFT,
} RB_Direction;

RB_Direction rb_direction_flip(RB_Direction direction) {
    return direction ^ 1;
}

typedef enum RB_Color {
    RBC_BLACK,
    RBC_RED,
} RB_Color;

RB_Color rb_color_flip(RB_Color color) {
    return color ^ 1;
}

typedef struct Node {
    int value;
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
    node->children[RB_COLOR_DIRECTION] = (Node*)(
        ((uintptr_t)node->children[RB_COLOR_DIRECTION] & NODE_PTR_MASK)
        | (uintptr_t)color
    );
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

Node *node_make(int value, RB_Color color) {
    Node *node = calloc(1, sizeof(*node));
    node->value = value;

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
    node_set_child(node, rb_direction_flip(direction), node_get_child(temp, direction));
    node_set_child(temp, direction, node);

    node_set_color(temp, node_get_color(node));
    node_set_color(node, RBC_RED);

    return temp;
}

Node *node_double_rotate(Node *node, RB_Direction direction) {
    node_set_child(node,
                   rb_direction_flip(direction),
                   node_rotate(node_get_child(node, rb_direction_flip(direction)),
                               rb_direction_flip(direction)));

    return node_rotate(node, direction);
}

typedef struct RB_Tree {
    Node *root;
} RB_Tree;

Node *rb_insert_fix_up(Node *node, RB_Direction direction) {
    if (node_child_red(node, direction)) {
        Node *dir_child = node_get_child(node, direction);

        if (node_child_red(node, rb_direction_flip(direction))) {
            if (
                node_child_red(dir_child, direction) ||
                node_child_red(dir_child, rb_direction_flip(direction))
            ) {
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

Node *rb_insert_helper(Node *node, int value) {
    if (node == NULL) {
        return node_make(value, RBC_RED);
    }

    RB_Direction direction = value > node->value;
    node_set_child(node, direction, rb_insert_helper(node_get_child(node, direction), value));

    return rb_insert_fix_up(node, direction);
}

void rb_insert(RB_Tree *tree, int value) {
    tree->root = rb_insert_helper(tree->root, value);
    node_set_color(tree->root, RBC_BLACK);
}

Node *rb_delete_fix_up(Node *node, RB_Direction direction, bool *ok) {
    Node *parent  = node;
    Node *sibling = node_get_child(node, rb_direction_flip(direction));

    if (node_red(sibling)) {
        node    = node_rotate(node, direction);
        sibling = node_get_child(parent, rb_direction_flip(direction));
    }

    if (sibling) {
        if (
            !node_child_red(sibling, RB_LEFT) &&
            !node_child_red(sibling, RB_RIGHT)
        ) {
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

Node *rb_delete_helper(Node *node, int value, bool *ok) {
    if (node == NULL) {
        *ok = true;
        return node;
    }

    if (node->value == value) {
        if (!node_has_child(node, RB_LEFT)
            || !node_has_child(node, RB_RIGHT)) {

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

            node->value = temp->value;
            value = temp->value;
        }
    }

    RB_Direction direction = value > node->value;
    node_set_child(node,
                   direction,
                   rb_delete_helper(node_get_child(node, direction), value, ok));

    return *ok ? node : rb_delete_fix_up(node, direction, ok);
}

void rb_delete(RB_Tree *tree, int value) {
    bool ok = false;
    tree->root = rb_delete_helper(tree->root, value, &ok);
    if (tree->root != NULL) {
        node_set_color(tree->root, RBC_BLACK);
    }
}

void rb_print_node(Node *node, const char *prefix, bool is_tail) {
    if (!node) {
        return;
    }

    printf("%s%s", prefix, is_tail ? "`-- " : "|-- ");
    printf("%d%c\n", node->value, node_get_color(node) == RBC_RED ? 'R' : 'B');

    char next_prefix[256];
    snprintf(next_prefix, sizeof(next_prefix), "%s%s", prefix, is_tail ? "    " : "|   ");

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

    printf("%d%c\n", tree->root->value, node_get_color(tree->root) == RBC_RED ? 'R' : 'B');

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
    RB_Tree tree = { 0 };

    int values[] = { 8, 3, 10, 1, 6, 14, 4, 7, 13 };

    printf("Insert values:\n");
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        printf("insert %d\n", values[i]);
        rb_insert(&tree, values[i]);
        rb_print(&tree);
    }

    printf("\nTree after inserts:\n");
    rb_print(&tree);

    printf("\nDelete 1:\n");
    rb_delete(&tree, 1);
    rb_print(&tree);

    printf("\nDelete 14:\n");
    rb_delete(&tree, 14);
    rb_print(&tree);

    rb_free(&tree);

    return 0;
}
