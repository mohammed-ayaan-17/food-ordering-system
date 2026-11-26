/*
 * ======================================================================================
 * VISVESVARAYA TECHNOLOGICAL UNIVERSITY (VTU), BELAGAVI
 * Scheme: 2025 Scheme | Semester: 3rd Semester B.E. (CSE & Allied Branches)
 * Course: Data Structures and Applications (Course Code: 1BCS305)
 *
 * MINI PROJECT TITLE: "FOOD ORDERING AND DELIVERY ASSIGNMENT SYSTEM"
 *
 * Syllabus Mapping (All 5 Modules of 1BCS305 Covered):
 *   - Module 1: Structures, Pointers, and Dynamic Memory Allocation (malloc/free)
 *   - Module 2: Stack ADT (LIFO) for Cart Action Undo ("Undo Last Added Item")
 *   - Module 3: Singly Linked List (Customer Cart), Linked Queue (Order Processing),
 *               and Doubly Linked List (Completed Order History with Forward/Reverse View)
 *   - Module 4: Binary Search Tree (BST) for Restaurant Menu Catalog (Search & Inorder)
 *   - Module 5: Graph ADT (City Zone Distance Matrix), Priority Queue (Min-Heap for
 *               Express/VIP Order & Delivery Partner Assignment), and Static Hashing
 *               with Linear Probing for O(1) Order Tracking by Order ID.
 * ======================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* --- CONSTANTS --- */
#define MAX_NAME 64
#define MAX_ADDR 128
#define MAX_ZONES 6
#define MAX_AGENTS 5
#define HASH_SIZE 23
#define INF 9999

/* ======================================================================================
 * MODULE 1 & 4: STRUCTURES & BINARY SEARCH TREE (BST) FOR MENU CATALOG
 * ====================================================================================== */
typedef struct MenuItem {
    int item_id;
    char name[MAX_NAME];
    char category[32];
    float price;
    struct MenuItem *left;
    struct MenuItem *right;
} MenuItem;

/* ======================================================================================
 * MODULE 2 & 3: SINGLY LINKED LIST FOR CUSTOMER CART & STACK FOR UNDO OPERATIONS
 * ====================================================================================== */
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

/* ======================================================================================
 * MODULE 3 & 5: PRIORITY QUEUE (MIN-HEAP) FOR ORDER PROCESSING & DOUBLY LINKED LIST
 * ====================================================================================== */
typedef struct Order {
    int order_id;
    char customer_name[MAX_NAME];
    char delivery_address[MAX_ADDR];  /* Open custom delivery address / landmark */
    int dest_zone;          /* Zone index: 1 to MAX_ZONES - 1 */
    int is_vip;             /* 1 = VIP/Express Order, 0 = Standard Order */
    int priority_score;     /* Lower value = Higher dispatch priority in Min-Heap */
    float total_amount;
    int assigned_agent_id;
    char assigned_agent_name[MAX_NAME];
    int delivery_dist_km;
    char status[32];        /* "QUEUED", "OUT_FOR_DELIVERY", "DELIVERED" */
    CartItem *items_head;   /* Deep copy of ordered items (SLL) */
} Order;

/* Min-Heap for Priority Order Queue (Module 5) */
#define MAX_HEAP_ORDERS 50
typedef struct {
    Order *heap[MAX_HEAP_ORDERS];
    int size;
} OrderPriorityQueue;

/* Doubly Linked List Node for Delivered Order History (Module 3) */
typedef struct HistoryNode {
    Order *order_data;
    struct HistoryNode *prev;
    struct HistoryNode *next;
} HistoryNode;

/* ======================================================================================
 * MODULE 5: GRAPH (CITY DELIVERY ZONES), DELIVERY AGENTS & STATIC HASH TABLE
 * ====================================================================================== */
typedef struct {
    int agent_id;
    char name[MAX_NAME];
    int current_zone;
    int is_available;       /* 1 = Available, 0 = Busy */
    int completed_deliveries;
    int active_order_id;    /* -1 = Idle, or Order ID currently assigned */
} DeliveryAgent;

/* Static Hash Table with Linear Probing for O(1) Order Lookup by Order ID */
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

/* City Zone Names and Weighted Graph Adjacency Matrix (Distances in km) */
const char *zone_names[MAX_ZONES] = {
    "Zone 0: Central Kitchen Hub (RPD Cross)",
    "Zone 1: Tilakwadi",
    "Zone 2: Shahapur",
    "Zone 3: Vadgaon",
    "Zone 4: Sadashiv Nagar",
    "Zone 5: Udyambag Industrial Area"
};

int city_graph[MAX_ZONES][MAX_ZONES] = {
    /* Z0  Z1  Z2  Z3  Z4  Z5 */
    {  0,  2,  4,  0,  6,  5 }, /* Z0: Central Kitchen Hub */
    {  2,  0,  3,  4,  0,  3 }, /* Z1: Tilakwadi */
    {  4,  3,  0,  2,  5,  0 }, /* Z2: Shahapur */
    {  0,  4,  2,  0,  0,  4 }, /* Z3: Vadgaon */
    {  6,  0,  5,  0,  0,  7 }, /* Z4: Sadashiv Nagar */
    {  5,  3,  0,  4,  7,  0 }  /* Z5: Udyambag */
};

/* ======================================================================================
 * MODULE 4 IMPLEMENTATION: BINARY SEARCH TREE (BST) FOR MENU OPERATIONS
 * ====================================================================================== */
MenuItem* createMenuItem(int id, const char *name, const char *category, float price) {
    MenuItem *newNode = (MenuItem*)malloc(sizeof(MenuItem));
    if (!newNode) {
        printf("Memory allocation error!\n");
        exit(EXIT_FAILURE);
    }
    newNode->item_id = id;
    strncpy(newNode->name, name, MAX_NAME - 1);
    newNode->name[MAX_NAME - 1] = '\0';
    strncpy(newNode->category, category, 31);
    newNode->category[31] = '\0';
    newNode->price = price;
    newNode->left = newNode->right = NULL;
    return newNode;
}

MenuItem* insertMenuBST(MenuItem *root, int id, const char *name, const char *category, float price) {
    if (root == NULL) {
        return createMenuItem(id, name, category, price);
    }
    if (id < root->item_id) {
        root->left = insertMenuBST(root->left, id, name, category, price);
    } else if (id > root->item_id) {
        root->right = insertMenuBST(root->right, id, name, category, price);
    } else {
        printf("  [!] Item ID %d already exists in the Menu BST.\n", id);
    }
    return root;
}

MenuItem* searchMenuBST(MenuItem *root, int id) {
    if (root == NULL || root->item_id == id) {
        return root;
    }
    if (id < root->item_id) {
        return searchMenuBST(root->left, id);
    }
    return searchMenuBST(root->right, id);
}

void displayMenuInorder(MenuItem *root) {
    if (root != NULL) {
        displayMenuInorder(root->left);
        printf("  | %-7d | %-26s | %-14s | Rs. %7.2f |\n",
               root->item_id, root->name, root->category, root->price);
        displayMenuInorder(root->right);
    }
}

/* ======================================================================================
 * MODULE 2 & 3 IMPLEMENTATION: SINGLY LINKED LIST (CART) & STACK (UNDO CART ACTION)
 * ====================================================================================== */
void pushUndo(int item_id, const char *name, int qty) {
    UndoNode *node = (UndoNode*)malloc(sizeof(UndoNode));
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

void addToCart(int item_id, int qty) {
    MenuItem *item = searchMenuBST(menu_root, item_id);
    if (item == NULL) {
        printf("  [!] Menu Item ID %d not found in BST Catalog.\n", item_id);
        return;
    }
    if (qty <= 0) {
        printf("  [!] Quantity must be greater than 0.\n");
        return;
    }

    /* Check if item already exists in Customer Cart SLL */
    CartItem *curr = cart_head;
    while (curr != NULL) {
        if (curr->item_id == item_id) {
            curr->quantity += qty;
            pushUndo(item_id, item->name, qty);
            printf("  [+] Updated '%s' quantity by +%d in Cart (New Qty: %d).\n",
                   item->name, qty, curr->quantity);
            return;
        }
        curr = curr->next;
    }

    /* Insert new node at end of Singly Linked List */
    CartItem *newNode = (CartItem*)malloc(sizeof(CartItem));
    newNode->item_id = item_id;
    strncpy(newNode->name, item->name, MAX_NAME - 1);
    newNode->name[MAX_NAME - 1] = '\0';
    newNode->quantity = qty;
    newNode->unit_price = item->price;
    newNode->next = NULL;

    if (cart_head == NULL) {
        cart_head = newNode;
    } else {
        CartItem *tail = cart_head;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = newNode;
    }

    pushUndo(item_id, item->name, qty);
    printf("  [+] Added %d x '%s' (Rs. %.2f each) to Cart SLL.\n",
           qty, item->name, item->price);
}

void undoLastCartAction(void) {
    if (undo_top == NULL) {
        printf("  [!] Undo Stack is empty. Nothing to undo!\n");
        return;
    }

    UndoNode *action = undo_top;
    undo_top = undo_top->next;

    CartItem *curr = cart_head;
    CartItem *prev = NULL;
    while (curr != NULL) {
        if (curr->item_id == action->item_id) {
            curr->quantity -= action->quantity;
            printf("  [UNDO] Reversed addition of %d x '%s' using Stack pop.\n",
                   action->quantity, action->name);
            if (curr->quantity <= 0) {
                if (prev == NULL) {
                    cart_head = curr->next;
                } else {
                    prev->next = curr->next;
                }
                free(curr);
                printf("         Removed '%s' from Cart SLL completely.\n", action->name);
            }
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    free(action);
}

void displayCart(void) {
    if (cart_head == NULL) {
        printf("\n  [Cart is currently empty]\n");
        return;
    }
    printf("\n  +---------+----------------------------+-------+------------+------------+\n");
    printf("  | Item ID | Item Name                  | Qty   | Unit Price | Subtotal   |\n");
    printf("  +---------+----------------------------+-------+------------+------------+\n");
    float grand_total = 0.0f;
    CartItem *curr = cart_head;
    while (curr != NULL) {
        float sub = curr->quantity * curr->unit_price;
        grand_total += sub;
        printf("  | %-7d | %-26s | %-5d | Rs. %6.2f | Rs. %6.2f |\n",
               curr->item_id, curr->name, curr->quantity, curr->unit_price, sub);
        curr = curr->next;
    }
    printf("  +---------+----------------------------+-------+------------+------------+\n");
    printf("  | GRAND TOTAL                                               | Rs. %6.2f |\n", grand_total);
    printf("  +-----------------------------------------------------------+------------+\n");
}

/* ======================================================================================
 * MODULE 5 IMPLEMENTATION: STATIC HASHING WITH LINEAR PROBING FOR O(1) ORDER LOOKUP
 * ====================================================================================== */
int hashFunction(int order_id) {
    return order_id % HASH_SIZE;
}

void insertOrderHash(Order *ord) {
    int idx = hashFunction(ord->order_id);
    int start_idx = idx;
    while (order_hash.table[idx] != NULL) {
        idx = (idx + 1) % HASH_SIZE;
        if (idx == start_idx) {
            printf("  [!] Order Hash Table is full!\n");
            return;
        }
    }
    order_hash.table[idx] = ord;
}

Order* searchOrderHash(int order_id, int *probes_out) {
    int idx = hashFunction(order_id);
    int start_idx = idx;
    int probes = 1;
    while (order_hash.table[idx] != NULL) {
        if (order_hash.table[idx]->order_id == order_id) {
            if (probes_out) *probes_out = probes;
            return order_hash.table[idx];
        }
        idx = (idx + 1) % HASH_SIZE;
        probes++;
        if (idx == start_idx) break;
    }
    if (probes_out) *probes_out = probes;
    return NULL;
}

/* ======================================================================================
 * MODULE 5 IMPLEMENTATION: MIN-HEAP PRIORITY QUEUE FOR ORDER SCHEDULING
 * ====================================================================================== */
void swapOrders(Order **a, Order **b) {
    Order *temp = *a;
    *a = *b;
    *b = temp;
}

void enqueueOrderPQ(Order *ord) {
    if (order_pq.size >= MAX_HEAP_ORDERS) {
        printf("  [!] Order Priority Queue is full!\n");
        return;
    }
    int i = order_pq.size++;
    order_pq.heap[i] = ord;

    /* Percolate Up (Min-Heap based on priority_score) */
    while (i != 0 && order_pq.heap[(i - 1) / 2]->priority_score > order_pq.heap[i]->priority_score) {
        swapOrders(&order_pq.heap[i], &order_pq.heap[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
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

void displayPendingOrdersPQ(void) {
    if (order_pq.size == 0) {
        printf("\n  [No pending orders in the Priority Queue]\n");
        return;
    }
    printf("\n  +----------+----------------------+---------+------------+--------------+\n");
    printf("  | Order ID | Customer Name        | Type    | Dest Zone  | Total Amount |\n");
    printf("  +----------+----------------------+---------+------------+--------------+\n");
    for (int i = 0; i < order_pq.size; i++) {
        Order *o = order_pq.heap[i];
        printf("  | %-8d | %-20s | %-7s | Zone %-5d | Rs. %8.2f |\n",
               o->order_id, o->customer_name,
               o->is_vip ? "EXPRESS" : "REGULAR",
               o->dest_zone, o->total_amount);
    }
    printf("  +----------+----------------------+---------+------------+--------------+\n");
}

/* ======================================================================================
 * MODULE 5 IMPLEMENTATION: DIJKSTRA'S SHORTEST PATH ON GRAPH FOR DELIVERY ASSIGNMENT
 * ====================================================================================== */
void computeShortestDistances(int src_zone, int dist[MAX_ZONES]) {
    int visited[MAX_ZONES] = {0};
    for (int i = 0; i < MAX_ZONES; i++) {
        dist[i] = INF;
    }
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

/* ======================================================================================
 * MODULE 3 IMPLEMENTATION: DOUBLY LINKED LIST FOR COMPLETED ORDER HISTORY
 * ====================================================================================== */
void appendToHistoryDLL(Order *ord) {
    HistoryNode *node = (HistoryNode*)malloc(sizeof(HistoryNode));
    node->order_data = ord;
    node->next = NULL;
    node->prev = history_tail;

    if (history_tail != NULL) {
        history_tail->next = node;
    } else {
        history_head = node;
    }
    history_tail = node;
}

void displayOrderHistoryDLL(int reverse) {
    if (history_head == NULL) {
        printf("\n  [Order History Doubly Linked List is empty]\n");
        return;
    }
    printf("\n  === COMPLETED ORDER HISTORY (%s TRAVERSAL OF DLL) ===\n",
           reverse ? "REVERSE (Newest to Oldest)" : "FORWARD (Oldest to Newest)");
    printf("  +----------+------------------+---------+------------------+----------+------------+\n");
    printf("  | Order ID | Customer         | Type    | Delivery Partner | Distance | Bill Total |\n");
    printf("  +----------+------------------+---------+------------------+----------+------------+\n");

    HistoryNode *curr = reverse ? history_tail : history_head;
    while (curr != NULL) {
        Order *o = curr->order_data;
        printf("  | %-8d | %-16s | %-7s | %-16s | %5d km | Rs. %6.2f |\n",
               o->order_id, o->customer_name,
               o->is_vip ? "EXPRESS" : "REGULAR",
               o->assigned_agent_name, o->delivery_dist_km, o->total_amount);
        curr = reverse ? curr->prev : curr->next;
    }
    printf("  +----------+------------------+---------+------------------+----------+------------+\n");
}

/* Auto-resolve city delivery zone from customer address (eliminating manual zone selection) */
int resolveZoneFromAddress(const char *address) {
    char lower[MAX_ADDR];
    int len = strlen(address);
    for (int i = 0; i < len && i < MAX_ADDR - 1; i++) {
        lower[i] = (char)tolower((unsigned char)address[i]);
    }
    lower[len < MAX_ADDR ? len : MAX_ADDR - 1] = '\0';

    if (strstr(lower, "tilakwadi") || strstr(lower, "congress") || strstr(lower, "rpd")) return 1;
    if (strstr(lower, "shahapur") || strstr(lower, "nath pai") || strstr(lower, "khade")) return 2;
    if (strstr(lower, "vadgaon") || strstr(lower, "angol") || strstr(lower, "madhavpur")) return 3;
    if (strstr(lower, "sadashiv") || strstr(lower, "shrinagar") || strstr(lower, "hindwadi")) return 4;
    if (strstr(lower, "udyambag") || strstr(lower, "industrial") || strstr(lower, "machhe")) return 5;

    /* Fallback hash-based mapping for any open address */
    unsigned long hash = 0;
    for (int i = 0; i < len; i++) {
        hash = (hash * 31 + (unsigned char)address[i]) % (MAX_ZONES - 1);
    }
    return (int)(hash + 1); /* 1 to 5 */
}

/* ======================================================================================
 * CORE WORKFLOWS: PLACE ORDER & ASSIGN NEAREST DELIVERY AGENT
 * ====================================================================================== */
void placeCustomerOrder(const char *cust_name, const char *address, int dest_zone, int is_vip) {
    if (cart_head == NULL) {
        printf("  [!] Cannot place order: Customer Cart is empty!\n");
        return;
    }
    if (dest_zone < 1 || dest_zone >= MAX_ZONES) {
        printf("  [!] Invalid destination zone (choose 1 to %d).\n", MAX_ZONES - 1);
        return;
    }

    Order *newOrder = (Order*)malloc(sizeof(Order));
    newOrder->order_id = next_order_seq++;
    strncpy(newOrder->customer_name, cust_name, MAX_NAME - 1);
    newOrder->customer_name[MAX_NAME - 1] = '\0';
    strncpy(newOrder->delivery_address, address, MAX_ADDR - 1);
    newOrder->delivery_address[MAX_ADDR - 1] = '\0';
    newOrder->dest_zone = dest_zone;
    newOrder->is_vip = is_vip ? 1 : 0;

    /* Priority Score calculation: VIP gets base 100, Regular gets base 1000 + arrival sequence */
    newOrder->priority_score = (is_vip ? 100 : 1000) + (arrival_counter++);
    newOrder->assigned_agent_id = -1;
    strcpy(newOrder->assigned_agent_name, "Unassigned");
    newOrder->delivery_dist_km = 0;
    strcpy(newOrder->status, "QUEUED");

    /* Move Cart SLL to Order and calculate total bill */
    float total = 0.0f;
    CartItem *curr = cart_head;
    while (curr != NULL) {
        total += curr->quantity * curr->unit_price;
        curr = curr->next;
    }
    if (is_vip) {
        total += 30.0f; /* Express handling fee */
    }
    newOrder->total_amount = total;
    newOrder->items_head = cart_head;
    cart_head = NULL; /* Reset active customer cart */
    clearUndoStack(); /* Clear cart undo stack after checkout */

    /* Enqueue into Min-Heap Priority Queue and index in Hash Table */
    enqueueOrderPQ(newOrder);
    insertOrderHash(newOrder);

    printf("\n  [ORDER CONFIRMED] Order ID: #%d | Customer: %s | Mode: %s\n",
           newOrder->order_id, newOrder->customer_name,
           newOrder->is_vip ? "EXPRESS (VIP Priority)" : "REGULAR");
    printf("                    Delivery Address: %s\n", newOrder->delivery_address);
    printf("                    Destination Zone: %s | Total Bill: Rs. %.2f\n",
           zone_names[dest_zone], newOrder->total_amount);
}

/* ======================================================================================
 * MODULE 5: RIDER DISPATCH (STAGE 1) & DELIVERY COMPLETION (STAGE 2)
 * ====================================================================================== */
void dispatchOrderToRider(void) {
    if (order_pq.size == 0) {
        printf("\n  [!] No pending orders in the Priority Queue to dispatch.\n");
        return;
    }

    /* Find nearest available delivery partner to Central Kitchen (Zone 0) + Destination Zone */
    int hub_dist[MAX_ZONES];
    computeShortestDistances(0, hub_dist);

    int best_agent_idx = -1;
    int min_pickup_dist = INF;

    for (int i = 0; i < MAX_AGENTS; i++) {
        if (agents[i].is_available) {
            int agent_to_hub = hub_dist[agents[i].current_zone];
            if (agent_to_hub < min_pickup_dist ||
               (agent_to_hub == min_pickup_dist && best_agent_idx != -1 &&
                agents[i].completed_deliveries < agents[best_agent_idx].completed_deliveries)) {
                min_pickup_dist = agent_to_hub;
                best_agent_idx = i;
            }
        }
    }

    if (best_agent_idx == -1) {
        printf("\n  [!] All delivery partners are currently busy out on delivery! Wait for an agent to complete delivery.\n");
        return;
    }

    Order *ord = dequeueOrderPQ();
    DeliveryAgent *ag = &agents[best_agent_idx];

    int total_trip_km = min_pickup_dist + hub_dist[ord->dest_zone];
    ord->assigned_agent_id = ag->agent_id;
    strncpy(ord->assigned_agent_name, ag->name, MAX_NAME - 1);
    ord->assigned_agent_name[MAX_NAME - 1] = '\0';
    ord->delivery_dist_km = total_trip_km;
    strcpy(ord->status, "OUT_FOR_DELIVERY");

    /* Mark partner as busy with this order */
    ag->is_available = 0;
    ag->active_order_id = ord->order_id;

    printf("\n  ======================================================================\n");
    printf("  [STAGE 1: ORDER DISPATCHED TO RIDER]\n");
    printf("  Order ID         : #%d (%s) for %s\n",
           ord->order_id, ord->is_vip ? "EXPRESS VIP" : "REGULAR", ord->customer_name);
    printf("  Delivery Address : %s\n", ord->delivery_address);
    printf("  Assigned Partner : %s (Agent ID: %d) [STATUS: BUSY / IN-TRANSIT]\n", ag->name, ag->agent_id);
    printf("  Route Planned    : Partner at Zone %d -> Hub (Zone 0) [%d km] -> %s [%d km]\n",
           ag->current_zone, min_pickup_dist, zone_names[ord->dest_zone], hub_dist[ord->dest_zone]);
    printf("  Total Route Dist : %d km (via Dijkstra's Shortest Path)\n", total_trip_km);
    printf("  Order Status     : OUT_FOR_DELIVERY (Waiting for Rider delivery confirmation)\n");
    printf("  ======================================================================\n");
}

void displayActiveDeliveries(void) {
    int active_found = 0;
    printf("\n  === ACTIVE RIDER DELIVERIES IN TRANSIT (OUT FOR DELIVERY) ===\n");
    printf("  +----------+----------------------+--------------------+--------------------------------------+----------+\n");
    printf("  | Order ID | Customer Name        | Assigned Partner   | Delivery Address                     | Distance |\n");
    printf("  +----------+----------------------+--------------------+--------------------------------------+----------+\n");
    for (int i = 0; i < HASH_SIZE; i++) {
        if (order_hash.table[i] != NULL && strcmp(order_hash.table[i]->status, "OUT_FOR_DELIVERY") == 0) {
            Order *o = order_hash.table[i];
            printf("  | %-8d | %-20s | %-18s | %-36.36s | %5d km |\n",
                   o->order_id, o->customer_name, o->assigned_agent_name, o->delivery_address, o->delivery_dist_km);
            active_found++;
        }
    }
    if (!active_found) {
        printf("  |                     No active in-transit deliveries at this time                                   |\n");
    }
    printf("  +----------+----------------------+--------------------+--------------------------------------+----------+\n");
}

void markOrderDeliveredByRider(int order_id) {
    Order *ord = searchOrderHash(order_id, NULL);
    if (ord == NULL) {
        printf("  [!] Order ID #%d not found in system.\n", order_id);
        return;
    }
    if (strcmp(ord->status, "DELIVERED") == 0) {
        printf("  [!] Order ID #%d has already been marked DELIVERED.\n", order_id);
        return;
    }
    if (strcmp(ord->status, "QUEUED") == 0) {
        printf("  [!] Order ID #%d is still QUEUED in the kitchen! Dispatch it to a rider first.\n", order_id);
        return;
    }

    /* Locate assigned agent */
    DeliveryAgent *ag = NULL;
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (agents[i].agent_id == ord->assigned_agent_id) {
            ag = &agents[i];
            break;
        }
    }

    /* Update order status */
    strcpy(ord->status, "DELIVERED");

    /* Free agent, update agent zone to destination, increment completed deliveries */
    if (ag != NULL) {
        ag->current_zone = ord->dest_zone;
        ag->is_available = 1;
        ag->active_order_id = -1;
        ag->completed_deliveries++;
    }

    /* Append to Doubly Linked List Delivered Order History */
    appendToHistoryDLL(ord);

    printf("\n  ======================================================================\n");
    printf("  [STAGE 2: DELIVERY CONFIRMED BY RIDER]\n");
    printf("  Order ID         : #%d successfully marked as DELIVERED!\n", ord->order_id);
    printf("  Customer         : %s | Destination: %s\n", ord->customer_name, zone_names[ord->dest_zone]);
    if (ag != NULL) {
        printf("  Rider Status     : %s (Agent ID: %d) is now AVAILABLE at Zone %d\n",
               ag->name, ag->agent_id, ag->current_zone);
        printf("  Rider Completed  : %d total successful deliveries\n", ag->completed_deliveries);
    }
    printf("  History Recorded : Appended to Doubly Linked List Order History (Module 3)\n");
    printf("  ======================================================================\n");
}

void displayDeliveryAgents(void) {
    int hub_dist[MAX_ZONES];
    computeShortestDistances(0, hub_dist);
    printf("\n  +----------+------------------+----------------------------+-------------+---------------+-----------+\n");
    printf("  | Agent ID | Partner Name     | Current Zone               | Dist to Hub | Status        | Completed |\n");
    printf("  +----------+------------------+----------------------------+-------------+---------------+-----------+\n");
    for (int i = 0; i < MAX_AGENTS; i++) {
        char status_str[32];
        if (agents[i].is_available) {
            strcpy(status_str, "AVAILABLE");
        } else {
            snprintf(status_str, sizeof(status_str), "BUSY (#%d)", agents[i].active_order_id);
        }
        printf("  | %-8d | %-16s | Zone %-2d (%-16.16s) | %8d km | %-13s | %-9d |\n",
               agents[i].agent_id, agents[i].name, agents[i].current_zone,
               zone_names[agents[i].current_zone] + 8,
               hub_dist[agents[i].current_zone], status_str, agents[i].completed_deliveries);
    }
    printf("  +----------+------------------+----------------------------+-------------+---------------+-----------+\n");
}

void trackOrderByHash(int order_id) {
    int probes = 0;
    Order *ord = searchOrderHash(order_id, &probes);
    if (ord == NULL) {
        printf("\n  [!] Order ID #%d not found in Hash Table (Probes: %d).\n", order_id, probes);
        return;
    }
    printf("\n  === HASH TABLE O(1) ORDER LOOKUP (Found in %d probe%s) ===\n",
           probes, probes == 1 ? "" : "s");
    printf("  Order ID         : #%d (Hash Bucket Index: %d)\n",
           ord->order_id, hashFunction(ord->order_id));
    printf("  Customer Name    : %s\n", ord->customer_name);
    printf("  Delivery Address : %s\n", ord->delivery_address);
    printf("  Priority Tier    : %s\n", ord->is_vip ? "EXPRESS (VIP)" : "REGULAR");
    printf("  Destination Zone : %s\n", zone_names[ord->dest_zone]);
    printf("  Current Status   : %s\n", ord->status);
    printf("  Assigned Partner : %s\n", ord->assigned_agent_name);
    printf("  Total Bill       : Rs. %.2f\n", ord->total_amount);
    printf("  Ordered Items (Singly Linked List):\n");
    CartItem *curr = ord->items_head;
    while (curr != NULL) {
        printf("    - %d x %s (Rs. %.2f each)\n", curr->quantity, curr->name, curr->unit_price);
        curr = curr->next;
    }
}

/* ======================================================================================
 * SYSTEM INITIALIZATION
 * ====================================================================================== */
void initializeSystemData(void) {
    order_pq.size = 0;
    for (int i = 0; i < HASH_SIZE; i++) {
        order_hash.table[i] = NULL;
    }

    /* Populate Menu Catalog into Binary Search Tree (Balanced insertion order) */
    menu_root = insertMenuBST(menu_root, 104, "Masala Dosa", "South Indian", 70.00f);
    menu_root = insertMenuBST(menu_root, 102, "Idli Vada Combo", "South Indian", 55.00f);
    menu_root = insertMenuBST(menu_root, 101, "Filter Coffee", "Beverages", 30.00f);
    menu_root = insertMenuBST(menu_root, 103, "Rava Upma", "South Indian", 45.00f);
    menu_root = insertMenuBST(menu_root, 106, "Paneer Butter Masala", "North Indian", 180.00f);
    menu_root = insertMenuBST(menu_root, 105, "Veg Biryani", "Main Course", 150.00f);
    menu_root = insertMenuBST(menu_root, 107, "Belagavi Kunda", "Desserts", 95.00f);

    /* Initialize Delivery Fleet across City Zones (all idle initially with active_order_id = -1) */
    agents[0] = (DeliveryAgent){201, "Ramesh Patil",   1, 1, 0, -1}; /* At Zone 1 (2 km from Hub) */
    agents[1] = (DeliveryAgent){202, "Suresh Kulkarni",2, 1, 0, -1}; /* At Zone 2 (4 km from Hub) */
    agents[2] = (DeliveryAgent){203, "Kiran Joshi",    3, 1, 0, -1}; /* At Zone 3 (6 km from Hub) */
    agents[3] = (DeliveryAgent){204, "Anand Desai",    5, 1, 0, -1}; /* At Zone 5 (5 km from Hub) */
    agents[4] = (DeliveryAgent){205, "Mahesh Hegde",   4, 1, 0, -1}; /* At Zone 4 (6 km from Hub) */
}

/* ======================================================================================
 * ROLE-SEPARATED PORTAL MENUS: USER END, AGENTS END & ADMIN END
 * ====================================================================================== */
void userPortalMenu(void) {
    int choice;
    while (1) {
        printf("\n--------------------------------------------------------------------------\n");
        printf("                       USER END: CUSTOMER PORTAL                          \n");
        printf("--------------------------------------------------------------------------\n");
        printf("  1. View Food Menu Catalog (BST Inorder Traversal - Module 4)\n");
        printf("  2. Add Item to Customer Cart (Singly Linked List - Module 3)\n");
        printf("  3. Undo Last Cart Action (LIFO Stack Pop - Module 2)\n");
        printf("  4. View Current Cart (Singly Linked List Traversal - Module 3)\n");
        printf("  5. Place Order (Enqueue into Min-Heap Priority Queue - Module 5)\n");
        printf("  6. Track Order Status by ID (Hash Table with Linear Probing - Module 5)\n");
        printf("  0. Return to Main Menu\n");
        printf("--------------------------------------------------------------------------\n");
        printf("  Enter Customer Portal choice: ");

        if (scanf("%d", &choice) != 1 || choice == 0) break;

        switch (choice) {
            case 1:
                printf("\n  +---------+----------------------------+----------------+-------------+\n");
                printf("  | Item ID | Dish Name                  | Category       | Unit Price  |\n");
                printf("  +---------+----------------------------+----------------+-------------+\n");
                displayMenuInorder(menu_root);
                printf("  +---------+----------------------------+----------------+-------------+\n");
                break;
            case 2: {
                int id, qty;
                printf("  Enter Menu Item ID to add: ");
                scanf("%d", &id);
                printf("  Enter Quantity: ");
                scanf("%d", &qty);
                addToCart(id, qty);
                break;
            }
            case 3:
                undoLastCartAction();
                break;
            case 4:
                displayCart();
                break;
            case 5: {
                if (cart_head == NULL) {
                    printf("  [!] Cart is empty! Add items before placing an order.\n");
                    break;
                }
                char cname[MAX_NAME];
                char addr[MAX_ADDR];
                int vip;
                getchar();
                printf("  Enter Customer Name: ");
                fgets(cname, MAX_NAME, stdin);
                cname[strcspn(cname, "\n")] = '\0';
                printf("  Enter Delivery Street Address / Flat / Landmark: ");
                fgets(addr, MAX_ADDR, stdin);
                addr[strcspn(addr, "\n")] = '\0';
                int zone = resolveZoneFromAddress(addr);
                printf("  Order Priority (0 = Regular, 1 = VIP Express [+Rs. 30]): ");
                scanf("%d", &vip);
                placeCustomerOrder(cname, addr, zone, vip);
                break;
            }
            case 6: {
                int oid;
                printf("  Enter Order ID to search (e.g., 1001): ");
                scanf("%d", &oid);
                trackOrderByHash(oid);
                break;
            }
            default:
                printf("  [!] Invalid selection. Choose 0-6.\n");
        }
    }
}

void agentAppointmentMenu(void) {
    int choice;
    while (1) {
        printf("\n--------------------------------------------------------------------------\n");
        printf("             AGENTS APPOINTMENT END: FLEET & DISPATCH CENTER              \n");
        printf("--------------------------------------------------------------------------\n");
        printf("  1. View Pending Orders Queue (Min-Heap Inspection - Module 5)\n");
        printf("  2. Stage 1: Dispatch Next Order to Nearest Rider (Status: OUT_FOR_DELIVERY)\n");
        printf("  3. View In-Transit Deliveries (Active Assigned Trips)\n");
        printf("  4. Stage 2: Mark Order as Delivered by Rider (Status: DELIVERED)\n");
        printf("  5. View Delivery Fleet Status & Availability (Graph - Module 5)\n");
        printf("  0. Return to Main Menu\n");
        printf("--------------------------------------------------------------------------\n");
        printf("  Enter Agent Appointment choice: ");

        if (scanf("%d", &choice) != 1 || choice == 0) break;

        switch (choice) {
            case 1:
                displayPendingOrdersPQ();
                break;
            case 2:
                dispatchOrderToRider();
                break;
            case 3:
                displayActiveDeliveries();
                break;
            case 4: {
                int oid;
                displayActiveDeliveries();
                printf("  Enter Order ID to mark as DELIVERED: ");
                scanf("%d", &oid);
                markOrderDeliveredByRider(oid);
                break;
            }
            case 5:
                displayDeliveryAgents();
                break;
            default:
                printf("  [!] Invalid selection. Choose 0-5.\n");
        }
    }
}

void adminPortalMenu(void) {
    int choice;
    while (1) {
        printf("\n--------------------------------------------------------------------------\n");
        printf("                     ADMIN END: SYSTEM & CATALOG PORTAL                   \n");
        printf("--------------------------------------------------------------------------\n");
        printf("  1. Add New Dish to Menu Catalog (BST Insertion - Module 4)\n");
        printf("  2. View Delivered Order History (Doubly Linked List - Module 3)\n");
        printf("  0. Return to Main Menu\n");
        printf("--------------------------------------------------------------------------\n");
        printf("  Enter Admin Portal choice: ");

        if (scanf("%d", &choice) != 1 || choice == 0) break;

        switch (choice) {
            case 1: {
                int id;
                char name[MAX_NAME], cat[32];
                float price;
                printf("  Enter new Item ID: ");
                scanf("%d", &id);
                getchar();
                printf("  Enter Dish Name: ");
                fgets(name, MAX_NAME, stdin);
                name[strcspn(name, "\n")] = '\0';
                printf("  Enter Category: ");
                fgets(cat, 32, stdin);
                cat[strcspn(cat, "\n")] = '\0';
                printf("  Enter Price (Rs.): ");
                scanf("%f", &price);
                menu_root = insertMenuBST(menu_root, id, name, cat, price);
                printf("  [+] Added '%s' (ID: %d) to Menu BST.\n", name, id);
                break;
            }
            case 2: {
                int rev;
                printf("  Traversal Order (0 = Forward [Oldest First], 1 = Reverse [Newest First]): ");
                scanf("%d", &rev);
                displayOrderHistoryDLL(rev);
                break;
            }
            default:
                printf("  [!] Invalid selection. Choose 0-2.\n");
        }
    }
}

/* ======================================================================================
 * MAIN MENU-DRIVEN CLI LOOP
 * ====================================================================================== */
int main(void) {
    initializeSystemData();

    int choice;
    while (1) {
        printf("\n==========================================================================\n");
        printf("                  FOOD ORDERING & DELIVERY ASSIGNMENT SYSTEM              \n");
        printf("==========================================================================\n");
        printf("  1. User End (Customer Ordering, Cart & Tracking Portal)\n");
        printf("  2. Agents Appointment End (Fleet Management & Order Dispatch)\n");
        printf("  3. Admin End (Menu BST Management & DLL Order History)\n");
        printf("  0. Exit System\n");
        printf("--------------------------------------------------------------------------\n");
        printf("  Enter your portal choice: ");

        if (scanf("%d", &choice) != 1 || choice == 0) {
            printf("\n  Thank you for using the Food Ordering & Assignment System!\n");
            break;
        }

        switch (choice) {
            case 1:
                userPortalMenu();
                break;
            case 2:
                agentAppointmentMenu();
                break;
            case 3:
                adminPortalMenu();
                break;
            default:
                printf("  [!] Invalid portal choice. Please select 0-3.\n");
        }
    }
    return 0;
}
