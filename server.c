/*
 * ======================================================================================
 * FOOD ORDERING & DELIVERY ASSIGNMENT SYSTEM - LIVE C HTTP BACKEND SERVER
 *
 * Implements all core data structures in C and exposes them via HTTP/JSON endpoints
 * while serving the separate User, Agents Appointment, Admin, Privacy, and Terms pages:
 *   - Menu Catalog: Binary Search Tree (BST)
 *   - Customer Cart: Singly Linked List (SLL) + LIFO Stack for Undo
 *   - Order Queue: Min-Heap Priority Queue (Express VIP vs Standard)
 *   - Delivery Routing: Weighted City Graph + Dijkstra's Shortest Path
 *   - Order Tracking: Static Hash Table with Linear Probing
 *   - Completed History: Doubly Linked List (Forward & Reverse Traversal)
 * ======================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef _WIN32
  #define _WINSOCK_DEPRECATED_NO_WARNINGS
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define CLOSE_SOCKET closesocket
  #define INVALID_SOCK INVALID_SOCKET
  #define strncasecmp _strnicmp
#else
  #include <unistd.h>
  #include <signal.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  typedef int sock_t;
  #define CLOSE_SOCKET close
  #define INVALID_SOCK (-1)
#endif

/* --- CONSTANTS --- */
#define MAX_NAME 64
#define MAX_ADDR 128
#define MAX_ZONES 6
#define MAX_AGENTS 5
#define HASH_SIZE 23
#define MAX_HEAP_ORDERS 50
#define INF 9999
#define REQ_BUF_SIZE 16384
#define RESP_BUF_SIZE 65536

/* ======================================================================================
 * DATA STRUCTURES
 * ====================================================================================== */
typedef struct MenuItem {
    int item_id;
    char name[MAX_NAME];
    char category[32];
    float price;
    struct MenuItem *left;
    struct MenuItem *right;
} MenuItem;

typedef struct CartItem {
    int item_id;
    char name[MAX_NAME];
    int quantity;
    float unit_price;
    struct CartItem *next;
} CartItem;

typedef struct UndoNode {
    int item_id;
    char name[MAX_NAME];
    int quantity;
    struct UndoNode *next;
} UndoNode;

typedef struct Order {
    int order_id;
    char customer_name[MAX_NAME];
    char delivery_address[MAX_ADDR];
    int dest_zone;
    int is_vip;
    int priority_score;
    float total_amount;
    int assigned_agent_id;
    char assigned_agent_name[MAX_NAME];
    int delivery_dist_km;
    char status[32]; /* "QUEUED", "OUT_FOR_DELIVERY", "DELIVERED" */
    CartItem *items_head;
} Order;

typedef struct {
    Order *heap[MAX_HEAP_ORDERS];
    int size;
} OrderPriorityQueue;

typedef struct HistoryNode {
    Order *order_data;
    struct HistoryNode *prev;
    struct HistoryNode *next;
} HistoryNode;

typedef struct {
    int agent_id;
    char name[MAX_NAME];
    int current_zone;
    int is_available;
    int completed_deliveries;
    int active_order_id;
} DeliveryAgent;

typedef struct {
    Order *table[HASH_SIZE];
} OrderHashTable;

/* --- GLOBAL SYSTEM STATE --- */
MenuItem *menu_root = NULL;
CartItem *cart_head = NULL;
UndoNode *undo_top = NULL;
OrderPriorityQueue order_pq;
HistoryNode *history_head = NULL;
HistoryNode *history_tail = NULL;
OrderHashTable order_hash;
DeliveryAgent agents[MAX_AGENTS];
int next_order_seq = 1001;
int arrival_counter = 1;

const char *zone_names[MAX_ZONES] = {
    "Zone 0: Central Kitchen Hub (RPD Cross)",
    "Zone 1: Tilakwadi",
    "Zone 2: Shahapur",
    "Zone 3: Vadgaon",
    "Zone 4: Sadashiv Nagar",
    "Zone 5: Udyambag Industrial Area"
};

int city_graph[MAX_ZONES][MAX_ZONES] = {
    { 0, 2, 4, 0, 6, 5 },
    { 2, 0, 3, 4, 0, 3 },
    { 4, 3, 0, 2, 5, 0 },
    { 0, 4, 2, 0, 0, 4 },
    { 6, 0, 5, 0, 0, 7 },
    { 5, 3, 0, 4, 7, 0 }
};

/* ======================================================================================
 * JSON / BUFFER HELPERS
 * ====================================================================================== */
void buf_appendf(char *buf, size_t *pos, size_t cap, const char *fmt, ...) {
    if (*pos >= cap) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, args);
    va_end(args);
    if (n > 0) {
        if ((size_t)n >= cap - *pos) {
            *pos = cap - 1;
        } else {
            *pos += (size_t)n;
        }
    }
}

void buf_append_json_str(char *buf, size_t *pos, size_t cap, const char *s) {
    buf_appendf(buf, pos, cap, "\"");
    for (const char *p = s; p && *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') buf_appendf(buf, pos, cap, "\\\"");
        else if (c == '\\') buf_appendf(buf, pos, cap, "\\\\");
        else if (c == '\n') buf_appendf(buf, pos, cap, "\\n");
        else if (c == '\r') buf_appendf(buf, pos, cap, "\\r");
        else if (c == '\t') buf_appendf(buf, pos, cap, "\\t");
        else if (c >= 32) buf_appendf(buf, pos, cap, "%c", c);
    }
    buf_appendf(buf, pos, cap, "\"");
}

static const char* find_json_val(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pattern)) != NULL) {
        const char *q = p + strlen(pattern);
        while (*q && isspace((unsigned char)*q)) q++;
        if (*q == ':') {
            q++;
            while (*q && isspace((unsigned char)*q)) q++;
            return q;
        }
        p += strlen(pattern);
    }
    return NULL;
}

int json_get_str(const char *json, const char *key, char *out, size_t max_len) {
    const char *v = find_json_val(json, key);
    if (!v || *v != '"') return 0;
    v++;
    size_t i = 0;
    while (*v && *v != '"' && i < max_len - 1) {
        if (*v == '\\' && *(v + 1)) {
            v++;
            if (*v == 'n') out[i++] = '\n';
            else if (*v == 'r') out[i++] = '\r';
            else if (*v == 't') out[i++] = '\t';
            else out[i++] = *v;
            v++;
        } else {
            out[i++] = *v++;
        }
    }
    out[i] = '\0';
    return 1;
}

int json_get_int(const char *json, const char *key, int def_val) {
    const char *v = find_json_val(json, key);
    if (!v) return def_val;
    char *end = NULL;
    long val = strtol(v, &end, 10);
    if (end == v) return def_val;
    return (int)val;
}

float json_get_float(const char *json, const char *key, float def_val) {
    const char *v = find_json_val(json, key);
    if (!v) return def_val;
    char *end = NULL;
    float val = strtof(v, &end);
    if (end == v) return def_val;
    return val;
}

int json_get_bool(const char *json, const char *key, int def_val) {
    const char *v = find_json_val(json, key);
    if (!v) return def_val;
    if (strncmp(v, "true", 4) == 0 || *v == '1') return 1;
    if (strncmp(v, "false", 5) == 0 || *v == '0') return 0;
    return def_val;
}

/* ======================================================================================
 * BST MENU CATALOG
 * ====================================================================================== */
MenuItem* createMenuItem(int id, const char *name, const char *category, float price) {
    MenuItem *node = (MenuItem*)malloc(sizeof(MenuItem));
    if (!node) exit(EXIT_FAILURE);
    node->item_id = id;
    strncpy(node->name, name, MAX_NAME - 1);
    node->name[MAX_NAME - 1] = '\0';
    strncpy(node->category, category, 31);
    node->category[31] = '\0';
    node->price = price;
    node->left = node->right = NULL;
    return node;
}

MenuItem* insertMenuBST(MenuItem *root, int id, const char *name, const char *category, float price, int *inserted) {
    if (root == NULL) {
        if (inserted) *inserted = 1;
        return createMenuItem(id, name, category, price);
    }
    if (id < root->item_id) {
        root->left = insertMenuBST(root->left, id, name, category, price, inserted);
    } else if (id > root->item_id) {
        root->right = insertMenuBST(root->right, id, name, category, price, inserted);
    } else {
        if (inserted) *inserted = 0;
    }
    return root;
}

MenuItem* searchMenuBST(MenuItem *root, int id) {
    if (root == NULL || root->item_id == id) return root;
    if (id < root->item_id) return searchMenuBST(root->left, id);
    return searchMenuBST(root->right, id);
}

void serializeMenuInorder(MenuItem *root, char *buf, size_t *pos, size_t cap, int *first) {
    if (!root) return;
    serializeMenuInorder(root->left, buf, pos, cap, first);
    if (!*first) buf_appendf(buf, pos, cap, ",");
    *first = 0;
    buf_appendf(buf, pos, cap, "{\"id\":%d,\"name\":", root->item_id);
    buf_append_json_str(buf, pos, cap, root->name);
    buf_appendf(buf, pos, cap, ",\"category\":");
    buf_append_json_str(buf, pos, cap, root->category);
    buf_appendf(buf, pos, cap, ",\"price\":%.2f}", root->price);
    serializeMenuInorder(root->right, buf, pos, cap, first);
}

void freeMenuBST(MenuItem *root) {
    if (!root) return;
    freeMenuBST(root->left);
    freeMenuBST(root->right);
    free(root);
}

/* ======================================================================================
 * CART SLL & UNDO STACK
 * ====================================================================================== */
void pushUndo(int item_id, const char *name, int qty) {
    UndoNode *node = (UndoNode*)malloc(sizeof(UndoNode));
    if (!node) return;
    node->item_id = item_id;
    strncpy(node->name, name, MAX_NAME - 1);
    node->name[MAX_NAME - 1] = '\0';
    node->quantity = qty;
    node->next = undo_top;
    undo_top = node;
}

void clearUndoStack(void) {
    while (undo_top != NULL) {
        UndoNode *temp = undo_top;
        undo_top = undo_top->next;
        free(temp);
    }
}

void freeCartList(CartItem *head) {
    while (head != NULL) {
        CartItem *tmp = head;
        head = head->next;
        free(tmp);
    }
}

int addToCart(int item_id, int qty, char *msg, size_t msg_cap) {
    MenuItem *item = searchMenuBST(menu_root, item_id);
    if (!item) {
        snprintf(msg, msg_cap, "Item ID %d not found in catalog.", item_id);
        return 0;
    }
    if (qty <= 0) {
        snprintf(msg, msg_cap, "Quantity must be greater than zero.");
        return 0;
    }

    CartItem *curr = cart_head;
    while (curr != NULL) {
        if (curr->item_id == item_id) {
            curr->quantity += qty;
            pushUndo(item_id, item->name, qty);
            snprintf(msg, msg_cap, "Added %s to cart.", item->name);
            return 1;
        }
        curr = curr->next;
    }

    CartItem *newNode = (CartItem*)malloc(sizeof(CartItem));
    if (!newNode) return 0;
    newNode->item_id = item_id;
    strncpy(newNode->name, item->name, MAX_NAME - 1);
    newNode->name[MAX_NAME - 1] = '\0';
    newNode->quantity = qty;
    newNode->unit_price = item->price;
    newNode->next = NULL;

    if (!cart_head) {
        cart_head = newNode;
    } else {
        CartItem *tail = cart_head;
        while (tail->next) tail = tail->next;
        tail->next = newNode;
    }

    pushUndo(item_id, item->name, qty);
    snprintf(msg, msg_cap, "Added %s to cart.", item->name);
    return 1;
}

int undoLastCartAction(char *msg, size_t msg_cap) {
    if (!undo_top) {
        snprintf(msg, msg_cap, "Nothing to undo.");
        return 0;
    }
    UndoNode *action = undo_top;
    undo_top = undo_top->next;

    CartItem *curr = cart_head;
    CartItem *prev = NULL;
    while (curr != NULL) {
        if (curr->item_id == action->item_id) {
            curr->quantity -= action->quantity;
            if (curr->quantity <= 0) {
                if (!prev) cart_head = curr->next;
                else prev->next = curr->next;
                free(curr);
            }
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    snprintf(msg, msg_cap, "Removed %d x %s from cart.", action->quantity, action->name);
    free(action);
    return 1;
}

/* ======================================================================================
 * HASH TABLE, MIN-HEAP PQ, DIJKSTRA & HISTORY DLL
 * ====================================================================================== */
int hashFunction(int order_id) {
    return order_id % HASH_SIZE;
}

int insertOrderHash(Order *ord) {
    int idx = hashFunction(ord->order_id);
    int start = idx;
    while (order_hash.table[idx] != NULL) {
        idx = (idx + 1) % HASH_SIZE;
        if (idx == start) return 0;
    }
    order_hash.table[idx] = ord;
    return 1;
}

Order* searchOrderHash(int order_id, int *probes_out) {
    int idx = hashFunction(order_id);
    int start = idx;
    int probes = 1;
    while (order_hash.table[idx] != NULL) {
        if (order_hash.table[idx]->order_id == order_id) {
            if (probes_out) *probes_out = probes;
            return order_hash.table[idx];
        }
        idx = (idx + 1) % HASH_SIZE;
        probes++;
        if (idx == start) break;
    }
    if (probes_out) *probes_out = probes;
    return NULL;
}

void swapOrders(Order **a, Order **b) {
    Order *tmp = *a;
    *a = *b;
    *b = tmp;
}

int enqueueOrderPQ(Order *ord) {
    if (order_pq.size >= MAX_HEAP_ORDERS) return 0;
    int i = order_pq.size++;
    order_pq.heap[i] = ord;
    while (i != 0 && order_pq.heap[(i - 1) / 2]->priority_score > order_pq.heap[i]->priority_score) {
        swapOrders(&order_pq.heap[i], &order_pq.heap[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
    return 1;
}

void minHeapify(int idx) {
    int smallest = idx;
    int left = 2 * idx + 1;
    int right = 2 * idx + 2;
    if (left < order_pq.size && order_pq.heap[left]->priority_score < order_pq.heap[smallest]->priority_score)
        smallest = left;
    if (right < order_pq.size && order_pq.heap[right]->priority_score < order_pq.heap[smallest]->priority_score)
        smallest = right;
    if (smallest != idx) {
        swapOrders(&order_pq.heap[idx], &order_pq.heap[smallest]);
        minHeapify(smallest);
    }
}

Order* dequeueOrderPQ(void) {
    if (order_pq.size <= 0) return NULL;
    if (order_pq.size == 1) {
        order_pq.size--;
        return order_pq.heap[0];
    }
    Order *root = order_pq.heap[0];
    order_pq.heap[0] = order_pq.heap[order_pq.size - 1];
    order_pq.size--;
    minHeapify(0);
    return root;
}

void computeShortestDistances(int src_zone, int dist[MAX_ZONES]) {
    int visited[MAX_ZONES] = {0};
    for (int i = 0; i < MAX_ZONES; i++) dist[i] = INF;
    dist[src_zone] = 0;

    for (int count = 0; count < MAX_ZONES - 1; count++) {
        int min_d = INF, u = -1;
        for (int v = 0; v < MAX_ZONES; v++) {
            if (!visited[v] && dist[v] <= min_d) {
                min_d = dist[v];
                u = v;
            }
        }
        if (u == -1) break;
        visited[u] = 1;
        for (int v = 0; v < MAX_ZONES; v++) {
            if (!visited[v] && city_graph[u][v] != 0 && dist[u] != INF &&
                dist[u] + city_graph[u][v] < dist[v]) {
                dist[v] = dist[u] + city_graph[u][v];
            }
        }
    }
}

int resolveZoneFromAddress(const char *address) {
    char lower[MAX_ADDR];
    size_t len = strlen(address);
    for (size_t i = 0; i < len && i < MAX_ADDR - 1; i++) {
        lower[i] = (char)tolower((unsigned char)address[i]);
    }
    lower[len < MAX_ADDR ? len : MAX_ADDR - 1] = '\0';

    if (strstr(lower, "tilakwadi") || strstr(lower, "congress") || strstr(lower, "rpd")) return 1;
    if (strstr(lower, "shahapur") || strstr(lower, "nath pai") || strstr(lower, "khade")) return 2;
    if (strstr(lower, "vadgaon") || strstr(lower, "angol") || strstr(lower, "madhavpur")) return 3;
    if (strstr(lower, "sadashiv") || strstr(lower, "shrinagar") || strstr(lower, "hindwadi")) return 4;
    if (strstr(lower, "udyambag") || strstr(lower, "industrial") || strstr(lower, "machhe")) return 5;

    unsigned long hash = 0;
    for (size_t i = 0; i < len; i++) {
        hash = (hash * 31 + (unsigned char)address[i]) % (MAX_ZONES - 1);
    }
    return (int)(hash + 1);
}

void appendToHistoryDLL(Order *ord) {
    HistoryNode *node = (HistoryNode*)malloc(sizeof(HistoryNode));
    if (!node) return;
    node->order_data = ord;
    node->next = NULL;
    node->prev = history_tail;
    if (history_tail) history_tail->next = node;
    else history_head = node;
    history_tail = node;
}

/* ======================================================================================
 * SYSTEM INITIALIZATION & RESET
 * ====================================================================================== */
void resetSystemState(void) {
    freeMenuBST(menu_root);
    menu_root = NULL;

    freeCartList(cart_head);
    cart_head = NULL;
    clearUndoStack();

    while (history_head) {
        HistoryNode *tmp = history_head;
        history_head = history_head->next;
        free(tmp);
    }
    history_tail = NULL;

    for (int i = 0; i < HASH_SIZE; i++) {
        if (order_hash.table[i]) {
            freeCartList(order_hash.table[i]->items_head);
            free(order_hash.table[i]);
            order_hash.table[i] = NULL;
        }
    }

    order_pq.size = 0;
    next_order_seq = 1001;
    arrival_counter = 1;

    menu_root = insertMenuBST(menu_root, 104, "Masala Dosa", "South Indian", 70.00f, NULL);
    menu_root = insertMenuBST(menu_root, 102, "Idli Vada Combo", "South Indian", 55.00f, NULL);
    menu_root = insertMenuBST(menu_root, 101, "Filter Coffee", "Beverages", 30.00f, NULL);
    menu_root = insertMenuBST(menu_root, 103, "Rava Upma", "South Indian", 45.00f, NULL);
    menu_root = insertMenuBST(menu_root, 106, "Paneer Butter Masala", "North Indian", 180.00f, NULL);
    menu_root = insertMenuBST(menu_root, 105, "Veg Biryani", "Main Course", 150.00f, NULL);
    menu_root = insertMenuBST(menu_root, 107, "Belagavi Kunda", "Desserts", 95.00f, NULL);

    agents[0] = (DeliveryAgent){201, "Ramesh Patil",    1, 1, 0, -1};
    agents[1] = (DeliveryAgent){202, "Suresh Kulkarni", 2, 1, 0, -1};
    agents[2] = (DeliveryAgent){203, "Kiran Joshi",     3, 1, 0, -1};
    agents[3] = (DeliveryAgent){204, "Anand Desai",     5, 1, 0, -1};
    agents[4] = (DeliveryAgent){205, "Mahesh Hegde",    4, 1, 0, -1};
}

/* ======================================================================================
 * BUSINESS OPERATIONS
 * ====================================================================================== */
Order* placeCustomerOrder(const char *cust_name, const char *address, int is_vip, char *err_msg, size_t err_cap) {
    if (!cart_head) {
        snprintf(err_msg, err_cap, "Cart is empty. Add at least one item before placing an order.");
        return NULL;
    }
    if (!cust_name || strlen(cust_name) == 0) {
        snprintf(err_msg, err_cap, "Please enter your full name.");
        return NULL;
    }
    if (!address || strlen(address) == 0) {
        snprintf(err_msg, err_cap, "Please enter your delivery street address.");
        return NULL;
    }

    Order *ord = (Order*)malloc(sizeof(Order));
    if (!ord) {
        snprintf(err_msg, err_cap, "Memory allocation failed.");
        return NULL;
    }

    ord->order_id = next_order_seq++;
    strncpy(ord->customer_name, cust_name, MAX_NAME - 1);
    ord->customer_name[MAX_NAME - 1] = '\0';
    strncpy(ord->delivery_address, address, MAX_ADDR - 1);
    ord->delivery_address[MAX_ADDR - 1] = '\0';
    ord->dest_zone = resolveZoneFromAddress(address);
    ord->is_vip = is_vip ? 1 : 0;
    ord->priority_score = (is_vip ? 100 : 1000) + (arrival_counter++);
    ord->assigned_agent_id = -1;
    strcpy(ord->assigned_agent_name, "Unassigned");
    ord->delivery_dist_km = 0;
    strcpy(ord->status, "QUEUED");

    float total = 0.0f;
    CartItem *curr = cart_head;
    while (curr) {
        total += curr->quantity * curr->unit_price;
        curr = curr->next;
    }
    if (ord->is_vip) total += 30.00f;
    ord->total_amount = total;
    ord->items_head = cart_head;

    if (!enqueueOrderPQ(ord)) {
        free(ord);
        snprintf(err_msg, err_cap, "Order queue is full.");
        return NULL;
    }
    if (!insertOrderHash(ord)) {
        snprintf(err_msg, err_cap, "Order storage limit reached in current session. Please reset.");
        return NULL;
    }

    cart_head = NULL;
    clearUndoStack();
    return ord;
}

Order* dispatchOrderToRider(char *msg, size_t msg_cap) {
    if (order_pq.size == 0) {
        snprintf(msg, msg_cap, "No pending orders in the dispatch queue.");
        return NULL;
    }

    int hub_dist[MAX_ZONES];
    computeShortestDistances(0, hub_dist);

    int best_idx = -1;
    int min_pickup = INF;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (agents[i].is_available) {
            int d = hub_dist[agents[i].current_zone];
            if (d < min_pickup ||
               (d == min_pickup && best_idx != -1 && agents[i].completed_deliveries < agents[best_idx].completed_deliveries)) {
                min_pickup = d;
                best_idx = i;
            }
        }
    }

    if (best_idx == -1) {
        snprintf(msg, msg_cap, "All riders are currently out on delivery.");
        return NULL;
    }

    Order *ord = dequeueOrderPQ();
    DeliveryAgent *ag = &agents[best_idx];

    int total_km = min_pickup + hub_dist[ord->dest_zone];
    ord->assigned_agent_id = ag->agent_id;
    strncpy(ord->assigned_agent_name, ag->name, MAX_NAME - 1);
    ord->assigned_agent_name[MAX_NAME - 1] = '\0';
    ord->delivery_dist_km = total_km;
    strcpy(ord->status, "OUT_FOR_DELIVERY");

    ag->is_available = 0;
    ag->active_order_id = ord->order_id;

    snprintf(msg, msg_cap, "Order #%d dispatched to %s (%d km route).", ord->order_id, ag->name, total_km);
    return ord;
}

Order* markOrderDeliveredByRider(int order_id, char *msg, size_t msg_cap) {
    Order *ord = searchOrderHash(order_id, NULL);
    if (!ord) {
        snprintf(msg, msg_cap, "Order #%d not found.", order_id);
        return NULL;
    }
    if (strcmp(ord->status, "OUT_FOR_DELIVERY") != 0) {
        snprintf(msg, msg_cap, "Order #%d is not currently out for delivery.", order_id);
        return NULL;
    }

    DeliveryAgent *ag = NULL;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (agents[i].agent_id == ord->assigned_agent_id) {
            ag = &agents[i];
            break;
        }
    }

    strcpy(ord->status, "DELIVERED");
    if (ag) {
        ag->current_zone = ord->dest_zone;
        ag->is_available = 1;
        ag->active_order_id = -1;
        ag->completed_deliveries++;
    }
    appendToHistoryDLL(ord);

    snprintf(msg, msg_cap, "Order #%d marked delivered by %s.", ord->order_id, ord->assigned_agent_name);
    return ord;
}

/* ======================================================================================
 * HTTP RESPONSE & STATIC FILE HELPERS
 * ====================================================================================== */
void send_all(sock_t client, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(client, data + sent, (int)(len - sent), 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

void send_http(sock_t client, const char *status, const char *content_type, const char *body, size_t body_len) {
    char header[1024];
    int hlen = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",
        status, content_type, body_len
    );
    send_all(client, header, (size_t)hlen);
    if (body && body_len > 0) send_all(client, body, body_len);
}

void send_json(sock_t client, const char *status, const char *json_body) {
    send_http(client, status, "application/json; charset=utf-8", json_body, strlen(json_body));
}

void serve_html_file(sock_t client, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        const char *not_found = "404 Not Found";
        send_http(client, "404 Not Found", "text/plain", not_found, strlen(not_found));
        return;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 0) {
        fclose(f);
        const char *err = "500 Internal Server Error";
        send_http(client, "500 Internal Server Error", "text/plain", err, strlen(err));
        return;
    }
    char *content = (char*)malloc((size_t)fsize + 1);
    if (!content) {
        fclose(f);
        return;
    }
    size_t read_bytes = fread(content, 1, (size_t)fsize, f);
    fclose(f);
    send_http(client, "200 OK", "text/html; charset=utf-8", content, read_bytes);
    free(content);
}

void serialize_order_json(Order *o, char *buf, size_t *pos, size_t cap) {
    buf_appendf(buf, pos, cap, "{\"order_id\":%d,\"customer_name\":", o->order_id);
    buf_append_json_str(buf, pos, cap, o->customer_name);
    buf_appendf(buf, pos, cap, ",\"delivery_address\":");
    buf_append_json_str(buf, pos, cap, o->delivery_address);
    buf_appendf(buf, pos, cap, ",\"dest_zone\":%d,\"zone_name\":", o->dest_zone);
    buf_append_json_str(buf, pos, cap, zone_names[o->dest_zone]);
    buf_appendf(buf, pos, cap, ",\"is_vip\":%s,\"priority_score\":%d,\"total_amount\":%.2f,\"status\":",
                o->is_vip ? "true" : "false", o->priority_score, o->total_amount);
    buf_append_json_str(buf, pos, cap, o->status);
    buf_appendf(buf, pos, cap, ",\"assigned_agent_id\":%d,\"assigned_agent_name\":", o->assigned_agent_id);
    buf_append_json_str(buf, pos, cap, o->assigned_agent_name);
    buf_appendf(buf, pos, cap, ",\"delivery_dist_km\":%d,\"items\":[", o->delivery_dist_km);

    CartItem *c = o->items_head;
    int first = 1;
    while (c) {
        if (!first) buf_appendf(buf, pos, cap, ",");
        first = 0;
        buf_appendf(buf, pos, cap, "{\"id\":%d,\"name\":", c->item_id);
        buf_append_json_str(buf, pos, cap, c->name);
        buf_appendf(buf, pos, cap, ",\"quantity\":%d,\"unit_price\":%.2f}", c->quantity, c->unit_price);
        c = c->next;
    }
    buf_appendf(buf, pos, cap, "]}");
}

/* ======================================================================================
 * REQUEST ROUTER
 * ====================================================================================== */
void handle_request(sock_t client, const char *method, const char *path_with_query, const char *body) {
    char path[256];
    const char *query = strchr(path_with_query, '?');
    if (query) {
        size_t plen = (size_t)(query - path_with_query);
        if (plen >= sizeof(path)) plen = sizeof(path) - 1;
        memcpy(path, path_with_query, plen);
        path[plen] = '\0';
        query++;
    } else {
        strncpy(path, path_with_query, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    if (strcmp(method, "OPTIONS") == 0) {
        send_http(client, "204 No Content", "text/plain", "", 0);
        return;
    }

    /* --- STATIC MULTI-PAGE ROUTES --- */
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0 || strcmp(path, "/user") == 0 || strcmp(path, "/user.html") == 0 || strcmp(path, "/index.html") == 0) {
            serve_html_file(client, "public/user.html");
            return;
        }
        if (strcmp(path, "/agents") == 0 || strcmp(path, "/agents.html") == 0) {
            serve_html_file(client, "public/agents.html");
            return;
        }
        if (strcmp(path, "/admin") == 0 || strcmp(path, "/admin.html") == 0) {
            serve_html_file(client, "public/admin.html");
            return;
        }
        if (strcmp(path, "/privacy") == 0 || strcmp(path, "/privacy.html") == 0) {
            serve_html_file(client, "public/privacy.html");
            return;
        }
        if (strcmp(path, "/terms") == 0 || strcmp(path, "/terms.html") == 0) {
            serve_html_file(client, "public/terms.html");
            return;
        }
        if (strcmp(path, "/health") == 0) {
            send_json(client, "200 OK", "{\"status\":\"ok\"}");
            return;
        }
        if (strcmp(path, "/favicon.ico") == 0) {
            const char *svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'><rect width='32' height='32' rx='6' fill='#15803d'/><path d='M9 11h14M9 16h14M9 21h9' stroke='#ffffff' stroke-width='2.5' stroke-linecap='round'/></svg>";
            send_http(client, "200 OK", "image/svg+xml", svg, strlen(svg));
            return;
        }
    }

    char *resp = (char*)malloc(RESP_BUF_SIZE);
    if (!resp) return;
    size_t pos = 0;

    /* --- API: GET /api/menu --- */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/menu") == 0) {
        int first = 1;
        buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"items\":[");
        serializeMenuInorder(menu_root, resp, &pos, RESP_BUF_SIZE, &first);
        buf_appendf(resp, &pos, RESP_BUF_SIZE, "]}");
        send_json(client, "200 OK", resp);
        free(resp);
        return;
    }

    /* --- API: POST /api/menu --- */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/menu") == 0) {
        int id = json_get_int(body, "id", -1);
        float price = json_get_float(body, "price", -1.0f);
        char name[MAX_NAME] = {0};
        char cat[32] = {0};
        json_get_str(body, "name", name, sizeof(name));
        json_get_str(body, "category", cat, sizeof(cat));

        if (id <= 0 || price <= 0.0f || strlen(name) == 0 || strlen(cat) == 0) {
            send_json(client, "400 Bad Request", "{\"ok\":false,\"error\":\"Valid positive ID, price, name, and category are required.\"}");
            free(resp);
            return;
        }
        int inserted = 0;
        menu_root = insertMenuBST(menu_root, id, name, cat, price, &inserted);
        if (!inserted) {
            snprintf(resp, RESP_BUF_SIZE, "{\"ok\":false,\"error\":\"Item ID %d already exists in catalog.\"}", id);
            send_json(client, "409 Conflict", resp);
        } else {
            snprintf(resp, RESP_BUF_SIZE, "{\"ok\":true,\"message\":\"Item #%d added to catalog.\"}", id);
            send_json(client, "200 OK", resp);
        }
        free(resp);
        return;
    }

    /* --- API: GET /api/cart --- */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/cart") == 0) {
        float subtotal = 0.0f;
        int total_qty = 0;
        buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"can_undo\":%s,\"items\":[", undo_top ? "true" : "false");
        CartItem *c = cart_head;
        int first = 1;
        while (c) {
            if (!first) buf_appendf(resp, &pos, RESP_BUF_SIZE, ",");
            first = 0;
            subtotal += c->quantity * c->unit_price;
            total_qty += c->quantity;
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"id\":%d,\"name\":", c->item_id);
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, c->name);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, ",\"quantity\":%d,\"unit_price\":%.2f}", c->quantity, c->unit_price);
            c = c->next;
        }
        buf_appendf(resp, &pos, RESP_BUF_SIZE, "],\"subtotal\":%.2f,\"total_count\":%d}", subtotal, total_qty);
        send_json(client, "200 OK", resp);
        free(resp);
        return;
    }

    /* --- API: POST /api/cart/add --- */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/cart/add") == 0) {
        int id = json_get_int(body, "id", -1);
        int qty = json_get_int(body, "quantity", 1);
        char msg[160] = {0};
        if (addToCart(id, qty, msg, sizeof(msg))) {
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"ok\":true,\"message\":");
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, msg);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "}");
            send_json(client, "200 OK", resp);
        } else {
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"ok\":false,\"error\":");
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, msg);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "}");
            send_json(client, "400 Bad Request", resp);
        }
        free(resp);
        return;
    }

    /* --- API: POST /api/cart/undo --- */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/cart/undo") == 0) {
        char msg[160] = {0};
        if (undoLastCartAction(msg, sizeof(msg))) {
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"ok\":true,\"message\":");
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, msg);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "}");
            send_json(client, "200 OK", resp);
        } else {
            send_json(client, "400 Bad Request", "{\"ok\":false,\"error\":\"Undo stack is empty.\"}");
        }
        free(resp);
        return;
    }

    /* --- API: POST /api/orders --- */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/orders") == 0) {
        char cname[MAX_NAME] = {0};
        char addr[MAX_ADDR] = {0};
        json_get_str(body, "customer_name", cname, sizeof(cname));
        json_get_str(body, "delivery_address", addr, sizeof(addr));
        int is_vip = json_get_bool(body, "is_vip", 0);

        char err[160] = {0};
        Order *ord = placeCustomerOrder(cname, addr, is_vip, err, sizeof(err));
        if (!ord) {
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"ok\":false,\"error\":");
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, err);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "}");
            send_json(client, "400 Bad Request", resp);
        } else {
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"ok\":true,\"order\":");
            serialize_order_json(ord, resp, &pos, RESP_BUF_SIZE);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "}");
            send_json(client, "200 OK", resp);
        }
        free(resp);
        return;
    }

    /* --- API: GET /api/orders/track?id=1001 --- */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/orders/track") == 0) {
        int oid = -1;
        if (query) {
            const char *idp = strstr(query, "id=");
            if (idp) oid = atoi(idp + 3);
        }
        if (oid <= 0) {
            send_json(client, "400 Bad Request", "{\"ok\":false,\"error\":\"Valid numeric Order ID required.\"}");
            free(resp);
            return;
        }
        int probes = 0;
        Order *ord = searchOrderHash(oid, &probes);
        if (!ord) {
            snprintf(resp, RESP_BUF_SIZE, "{\"ok\":false,\"error\":\"Order #%d not found.\"}", oid);
            send_json(client, "404 Not Found", resp);
        } else {
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"ok\":true,\"probes\":%d,\"bucket\":%d,\"order\":", probes, hashFunction(oid));
            serialize_order_json(ord, resp, &pos, RESP_BUF_SIZE);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "}");
            send_json(client, "200 OK", resp);
        }
        free(resp);
        return;
    }

    /* --- API: GET /api/agents/state --- */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/agents/state") == 0) {
        int hub_dist[MAX_ZONES];
        computeShortestDistances(0, hub_dist);

        /* Priority Queue pending orders (sorted view via copy of heap pointers) */
        Order *sorted_pq[MAX_HEAP_ORDERS];
        for (int i = 0; i < order_pq.size; i++) sorted_pq[i] = order_pq.heap[i];
        for (int i = 0; i < order_pq.size; i++) {
            for (int j = i + 1; j < order_pq.size; j++) {
                if (sorted_pq[j]->priority_score < sorted_pq[i]->priority_score) {
                    Order *tmp = sorted_pq[i];
                    sorted_pq[i] = sorted_pq[j];
                    sorted_pq[j] = tmp;
                }
            }
        }

        buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"pending\":[");
        for (int i = 0; i < order_pq.size; i++) {
            if (i > 0) buf_appendf(resp, &pos, RESP_BUF_SIZE, ",");
            serialize_order_json(sorted_pq[i], resp, &pos, RESP_BUF_SIZE);
        }

        buf_appendf(resp, &pos, RESP_BUF_SIZE, "],\"active_deliveries\":[");
        int first_act = 1;
        for (int i = 0; i < HASH_SIZE; i++) {
            if (order_hash.table[i] && strcmp(order_hash.table[i]->status, "OUT_FOR_DELIVERY") == 0) {
                if (!first_act) buf_appendf(resp, &pos, RESP_BUF_SIZE, ",");
                first_act = 0;
                serialize_order_json(order_hash.table[i], resp, &pos, RESP_BUF_SIZE);
            }
        }

        buf_appendf(resp, &pos, RESP_BUF_SIZE, "],\"fleet\":[");
        for (int i = 0; i < MAX_AGENTS; i++) {
            if (i > 0) buf_appendf(resp, &pos, RESP_BUF_SIZE, ",");
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"id\":%d,\"name\":", agents[i].agent_id);
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, agents[i].name);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, ",\"zone\":%d,\"available\":%s,\"completed\":%d,\"active_order_id\":%d,\"dist_to_hub\":%d}",
                        agents[i].current_zone,
                        agents[i].is_available ? "true" : "false",
                        agents[i].completed_deliveries,
                        agents[i].active_order_id,
                        hub_dist[agents[i].current_zone]);
        }

        buf_appendf(resp, &pos, RESP_BUF_SIZE, "],\"hub_distances\":[");
        for (int z = 1; z < MAX_ZONES; z++) {
            if (z > 1) buf_appendf(resp, &pos, RESP_BUF_SIZE, ",");
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"zone\":%d,\"name\":", z);
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, zone_names[z]);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, ",\"km\":%d}", hub_dist[z]);
        }
        buf_appendf(resp, &pos, RESP_BUF_SIZE, "]}");

        send_json(client, "200 OK", resp);
        free(resp);
        return;
    }

    /* --- API: POST /api/agents/dispatch --- */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/agents/dispatch") == 0) {
        char msg[160] = {0};
        Order *ord = dispatchOrderToRider(msg, sizeof(msg));
        if (!ord) {
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"ok\":false,\"error\":");
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, msg);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "}");
            send_json(client, "400 Bad Request", resp);
        } else {
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"ok\":true,\"message\":");
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, msg);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, ",\"order\":");
            serialize_order_json(ord, resp, &pos, RESP_BUF_SIZE);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "}");
            send_json(client, "200 OK", resp);
        }
        free(resp);
        return;
    }

    /* --- API: POST /api/agents/deliver --- */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/agents/deliver") == 0) {
        int oid = json_get_int(body, "order_id", -1);
        char msg[160] = {0};
        Order *ord = markOrderDeliveredByRider(oid, msg, sizeof(msg));
        if (!ord) {
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"ok\":false,\"error\":");
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, msg);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "}");
            send_json(client, "400 Bad Request", resp);
        } else {
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"ok\":true,\"message\":");
            buf_append_json_str(resp, &pos, RESP_BUF_SIZE, msg);
            buf_appendf(resp, &pos, RESP_BUF_SIZE, "}");
            send_json(client, "200 OK", resp);
        }
        free(resp);
        return;
    }

    /* --- API: GET /api/admin/history?reverse=0|1 --- */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/admin/history") == 0) {
        int reverse = 0;
        if (query && strstr(query, "reverse=1")) reverse = 1;

        buf_appendf(resp, &pos, RESP_BUF_SIZE, "{\"reverse\":%s,\"orders\":[", reverse ? "true" : "false");
        HistoryNode *curr = reverse ? history_tail : history_head;
        int first = 1;
        while (curr) {
            if (!first) buf_appendf(resp, &pos, RESP_BUF_SIZE, ",");
            first = 0;
            serialize_order_json(curr->order_data, resp, &pos, RESP_BUF_SIZE);
            curr = reverse ? curr->prev : curr->next;
        }
        buf_appendf(resp, &pos, RESP_BUF_SIZE, "]}");
        send_json(client, "200 OK", resp);
        free(resp);
        return;
    }

    /* --- API: POST /api/reset --- */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/reset") == 0) {
        resetSystemState();
        send_json(client, "200 OK", "{\"ok\":true,\"message\":\"Session reset.\"}");
        free(resp);
        return;
    }

    send_json(client, "404 Not Found", "{\"error\":\"Endpoint not found\"}");
    free(resp);
}

/* ======================================================================================
 * SERVER MAIN LOOP
 * ====================================================================================== */
int main(void) {
    resetSystemState();

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    int port = 8080;
    const char *env_port = getenv("PORT");
    if (env_port && atoi(env_port) > 0) {
        port = atoi(env_port);
    }

    sock_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCK) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Bind failed on port %d\n", port);
        CLOSE_SOCKET(server_fd);
        return 1;
    }

    if (listen(server_fd, 32) < 0) {
        fprintf(stderr, "Listen failed\n");
        CLOSE_SOCKET(server_fd);
        return 1;
    }

    printf("Belagavi Kitchen Direct C backend running on http://0.0.0.0:%d\n", port);
    fflush(stdout);

    char *req_buf = (char*)malloc(REQ_BUF_SIZE);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        sock_t client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client == INVALID_SOCK) continue;

        int total_read = 0;
        char *hdr_end = NULL;
        while (total_read < REQ_BUF_SIZE - 1) {
            int n = recv(client, req_buf + total_read, REQ_BUF_SIZE - 1 - total_read, 0);
            if (n <= 0) break;
            total_read += n;
            req_buf[total_read] = '\0';
            hdr_end = strstr(req_buf, "\r\n\r\n");
            if (hdr_end) break;
        }

        if (hdr_end) {
            int content_len = 0;
            const char *cl = req_buf;
            while (cl < hdr_end) {
                if (strncasecmp(cl, "Content-Length:", 15) == 0) {
                    content_len = atoi(cl + 15);
                    break;
                }
                const char *nl = strstr(cl, "\r\n");
                if (!nl) break;
                cl = nl + 2;
            }

            size_t hdr_bytes = (size_t)(hdr_end + 4 - req_buf);
            while (total_read < (int)(hdr_bytes + (size_t)content_len) && total_read < REQ_BUF_SIZE - 1) {
                int n = recv(client, req_buf + total_read, REQ_BUF_SIZE - 1 - total_read, 0);
                if (n <= 0) break;
                total_read += n;
            }
            req_buf[total_read] = '\0';

            char method[16] = {0};
            char path[256] = {0};
            if (sscanf(req_buf, "%15s %255s", method, path) == 2) {
                const char *body = hdr_end + 4;
                handle_request(client, method, path, body);
            }
        }

        CLOSE_SOCKET(client);
    }

    free(req_buf);
    CLOSE_SOCKET(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
