#include <Python.h>

/*
 * This is a project forked from https://github.com/amitdev/lru-dict and I added ttl feature for it.
 * 
 * This is a simple implementation of LRU Dict that uses a Python dict and an associated doubly linked
 * list to keep track of recently inserted/accessed items. It also supports TTL feature to expire items
 * which makes it more suitable to be a cache
 *
 * Dict will store: key -> Node mapping, where Node is a linked list node.
 * The Node itself will contain the value, the key, as well as the expire time.
 * TTL is in nanoseconds, we can save some conversion time because Python C-API use ns to represent time.
 * 
 * For eg, two items with default 60s ttl and one item with specified 10s ttl:
 *
 * >>> l = TTLRU(2, 60*1000000000)
 * >>> l[0] = 'foo'
 * >>> l[1] = 'bar'
 * >>> l.set_with_ttl(2, 'baz', 10*1000000000)
 *
 * can be visualised as:
 *
 *             ---+--(hash(0)--+--hash(1)--+
 * self->dict  ...|            |           |
 *             ---+-----|------+---------|-+
 *                      |                |
 *                +-----v------+   +-----v------+
 * self->first--->|<'foo'>, <0>|-->|<'bar'>, <1>|<---self->last
 *             +--|            |<--|            |--+
 *             |  +------------+   +------------+  |
 *             v                                   v
 *           NULL                                 NULL
 *
 *  The invariant is to maintain the list to reflect the LRU order of items in the dict.
 *  self->first will point to the MRU item and self-last to LRU item. Size of list will not
 *  grow beyond size of TTLRU dict.
 *
 */

#ifndef Py_TYPE
 #define Py_TYPE(ob) (((PyObject*)(ob))->ob_type)
#endif

#define GET_NODE(d, key) (Node *) Py_TYPE(d)->tp_as_mapping->mp_subscript((d), (key))
#define PUT_NODE(d, key, node) Py_TYPE(d)->tp_as_mapping->mp_ass_subscript((d), (key), ((PyObject *)node))

#define IS_EXPIRED(t_now, node) (t_now > node->expire && node->expire != -1)

/* If someone figures out how to enable debug builds with setuptools, you can delete this */
#if 0
#undef assert
#define str(s) #s
#define assert(v) \
  do {                                                                                \
    if (!(v)) {                                                                       \
      fprintf(stderr, "Assertion failed: %s on %s:%d\n",                              \
              str(v), __FILE__, __LINE__);                                            \
      fflush(stderr);                                                                 \
      abort();                                                                        \
    }                                                                                 \
  } while(0)
#endif

typedef struct _Node {
    PyObject_HEAD
    PyObject * value;
    PyObject * key;
    _PyTime_t expire;
    struct _Node * prev;
    struct _Node * next;
} Node;

static void
node_dealloc(Node* self)
{
    Py_DECREF(self->key);
    Py_DECREF(self->value);
    assert(self->prev == NULL);
    assert(self->next == NULL);
    PyObject_Del((PyObject*)self);
}

static PyObject*
node_repr(Node* self)
{
    return PyObject_Repr(self->value);
}

static PyTypeObject NodeType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "ttlru.Node",              /* tp_name */
    sizeof(Node),            /* tp_basicsize */
    0,                       /* tp_itemsize */
    (destructor)node_dealloc,/* tp_dealloc */
    0,                       /* tp_print */
    0,                       /* tp_getattr */
    0,                       /* tp_setattr */
    0,                       /* tp_compare */
    (reprfunc)node_repr,     /* tp_repr */
    0,                       /* tp_as_number */
    0,                       /* tp_as_sequence */
    0,                       /* tp_as_mapping */
    0,                       /* tp_hash */
    0,                       /* tp_call */
    0,                       /* tp_str */
    0,                       /* tp_getattro */
    0,                       /* tp_setattro */
    0,                       /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,      /* tp_flags */
    "Linked List Node",      /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    0,                       /* tp_methods */
    0,                       /* tp_members */
    0,                       /* tp_getset */
    0,                       /* tp_base */
    0,                       /* tp_dict */
    0,                       /* tp_descr_get */
    0,                       /* tp_descr_set */
    0,                       /* tp_dictoffset */
    0,                       /* tp_init */
    0,                       /* tp_alloc */
    0,                       /* tp_new */
};

typedef struct {
    PyObject_HEAD
    PyObject * dict;
    Node * first;
    Node * last;
    Py_ssize_t size;
    Py_ssize_t hits;
    Py_ssize_t misses;
    PyObject *callback;
    _PyTime_t default_ttl;
} LRU;


static PyObject *
set_callback(LRU *self, PyObject *args)
{
    PyObject *result = NULL;
    PyObject *temp;

    if (PyArg_ParseTuple(args, "O:set_callback", &temp)) {
        if (temp == Py_None) {
            Py_XDECREF(self->callback);
            self->callback = NULL;
        } else if (!PyCallable_Check(temp)) {
            PyErr_SetString(PyExc_TypeError, "parameter must be callable");
            return NULL;
        } else {
            Py_XINCREF(temp);            /* Add a reference to new callback */
            Py_XDECREF(self->callback);  /* Dispose of previous callback */
            self->callback = temp;       /* Remember new callback */
        }
        Py_RETURN_NONE;
    }
    return result;
}

static void
lru_remove_node(LRU *self, Node* node)
{
    if (self->first == node) {
        self->first = node->next;
    }
    if (self->last == node) {
        self->last = node->prev;
    }
    if (node->prev) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }
    node->next = node->prev = NULL;
}

static void
lru_add_node_at_head(LRU *self, Node* node)
{
    node->prev = NULL;
    if (!self->first) {
        self->first = self->last = node;
        node->next = NULL;
    } else {
        node->next = self->first;
        if (node->next) {
            node->next->prev = node;
        }
        self->first = node;
    }
}

static void
lru_delete_last(LRU *self)
{
    PyObject *arglist;
    PyObject *result;
    Node* n = self->last;

    if (!self->last)
        return;

    if (self->callback) {
        
        arglist = Py_BuildValue("OO", n->key, n->value);
        result = PyObject_CallObject(self->callback, arglist);
        Py_XDECREF(result);
        Py_DECREF(arglist);
    }

    lru_remove_node(self, n);
    PUT_NODE(self->dict, n->key, NULL);
}



static void
lru_delete_expire(LRU *self, Node* n)
{
    PyObject *arglist;
    PyObject *result;


    if (self->callback) {
        
        arglist = Py_BuildValue("OO", n->key, n->value);
        result = PyObject_CallObject(self->callback, arglist);
        Py_XDECREF(result);
        Py_DECREF(arglist);
    }

    lru_remove_node(self, n);
    PUT_NODE(self->dict, n->key, NULL);
}



static Py_ssize_t
lru_length(LRU *self)
{
    return PyDict_Size(self->dict);
}


static int
LRU_contains_check_with_ttl(LRU *self, PyObject *key)
{
    _PyTime_t t_now;

    Node *node = GET_NODE(self->dict, key);
    PyErr_Clear();  /* GET_NODE sets an exception on miss. Shut it up. */
    if (!node) {
        return 0;
    }
    
    assert(PyObject_TypeCheck(node, &NodeType));

    t_now = _PyTime_GetSystemClock();
    if (IS_EXPIRED(t_now, node)){
        lru_delete_expire(self, node);
        Py_DECREF(node);
        return 0;
    }
    return 1;
}

static PyObject *
LRU_contains_key(LRU *self, PyObject *key)
{
    if (LRU_contains_check_with_ttl(self, key)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyObject *
LRU_contains(LRU *self, PyObject *args)
{
    PyObject *key;
    if (!PyArg_ParseTuple(args, "O", &key))
        return NULL;
    return LRU_contains_key(self, key);
}

static int
LRU_seq_contains(LRU *self, PyObject *key)
{
    return LRU_contains_check_with_ttl(self, key);
}

static PyObject *
lru_subscript(LRU *self, register PyObject *key)
{
    _PyTime_t t_now;

    Node *node = GET_NODE(self->dict, key);
    if (!node) {
        self->misses++;
        return NULL;
    }

    assert(PyObject_TypeCheck(node, &NodeType));

    t_now = _PyTime_GetSystemClock();
    if (IS_EXPIRED(t_now, node)){
        lru_delete_expire(self, node);
        Py_DECREF(node); 
        self->misses++;
        return NULL;
    }

    /* We don't need to move the node when it's already self->first. */
    if (node != self->first) {
        lru_remove_node(self, node);
        lru_add_node_at_head(self, node);
    }

    self->hits++;
    Py_INCREF(node->value);
    Py_DECREF(node);
    return node->value;
}

static PyObject *
LRU_get(LRU *self, PyObject *args)
{
    PyObject *key;
    PyObject *instead = NULL;
    PyObject *result;

    if (!PyArg_ParseTuple(args, "O|O", &key, &instead))
        return NULL;

    result = lru_subscript(self, key);
    PyErr_Clear();  /* GET_NODE sets an exception on miss. Shut it up. */
    if (result)
        return result;

    if (!instead) {
        Py_RETURN_NONE;
    }

    Py_INCREF(instead);
    return instead;
}

static int
LRU_ass_sub_ttl(LRU *self, PyObject *key, PyObject *value, _PyTime_t ttl)
{
    int res = 0;
    _PyTime_t t_now;
    Node *node = GET_NODE(self->dict, key);
    PyErr_Clear();  /* GET_NODE sets an exception on miss. Shut it up. */

    if (value) {
        if (node) {
            Py_INCREF(value);
            Py_DECREF(node->value);
            node->value = value;

            lru_remove_node(self, node);
            lru_add_node_at_head(self, node);

            res = 0;
        } else {
            node = PyObject_NEW(Node, &NodeType);
            node->key = key;
            node->value = value;
            node->next = node->prev = NULL;

            Py_INCREF(key);
            Py_INCREF(value);

            res = PUT_NODE(self->dict, key, node);
            if (res == 0) {
                if (lru_length(self) > self->size) {
                    lru_delete_last(self);
                }

                lru_add_node_at_head(self, node);
            }
        }
        if (ttl == -1)
            node->expire = -1;
        else{
            t_now = _PyTime_GetSystemClock();
            node->expire = t_now + ttl;
        }
    } else {
        res = PUT_NODE(self->dict, key, NULL);
        if (res == 0) {
            assert(node && PyObject_TypeCheck(node, &NodeType));
            lru_remove_node(self, node);
        }
    }

    Py_XDECREF(node);
    return res;
}

static int
lru_ass_sub(LRU *self, PyObject *key, PyObject *value)
{
    return LRU_ass_sub_ttl(self, key, value, self->default_ttl);
}

static PyObject *
LRU_set_with_ttl(LRU *self, PyObject *args)
{
    PyObject *key;
    PyObject *value;
    _PyTime_t ttl;
    if (!PyArg_ParseTuple(args, "OOL", &key, &value, &ttl))
        return NULL;
    LRU_ass_sub_ttl(self, key, value, ttl);
    Py_RETURN_NONE;
}

static PyMappingMethods LRU_as_mapping = {
    (lenfunc)lru_length,        /*mp_length*/
    (binaryfunc)lru_subscript,  /*mp_subscript*/
    (objobjargproc)lru_ass_sub, /*mp_ass_subscript*/
};


static PyObject *
get_item(Node *node)
{
    PyObject *tuple = PyTuple_New(2);
    Py_INCREF(node->key);
    PyTuple_SET_ITEM(tuple, 0, node->key);
    Py_INCREF(node->value);
    PyTuple_SET_ITEM(tuple, 1, node->value);
    return tuple;
}

static PyObject *
collect(LRU *self, PyObject * (*getterfunc)(Node *))
{
    register PyObject *v;
    Node *curr, *need_delete;
    int i;
    _PyTime_t t_now;
    Py_ssize_t old_length;

    old_length = lru_length(self);
    v = PyList_New(old_length);
    if (v == NULL)
        return NULL;
    curr = self->first;
    i = 0;
    t_now = _PyTime_GetSystemClock();

    while (curr) {
        if (IS_EXPIRED(t_now, curr)){
            need_delete = curr;
            curr = curr->next;
            lru_remove_node(self, need_delete);
            PUT_NODE(self->dict, need_delete->key, NULL);
        } else {
            PyList_SET_ITEM(v, i++, getterfunc(curr));
            curr = curr->next;    
        }
    }
    assert(i == lru_length(self));
    PyList_SetSlice(v, i, old_length, NULL);
    return v;
}

static PyObject *
get_key(Node *node)
{
    Py_INCREF(node->key);
    return node->key;
}

static PyObject *
LRU_update(LRU *self, PyObject *args, PyObject *kwargs)
{
	PyObject *key, *value;
	PyObject *arg = NULL;
	Py_ssize_t pos = 0;

	if ((PyArg_ParseTuple(args, "|O", &arg))) {
		if (arg && PyDict_Check(arg)) {
			while (PyDict_Next(arg, &pos, &key, &value))
				lru_ass_sub(self, key, value);
		}
	}
	
	if (kwargs != NULL && PyDict_Check(kwargs)) {
		while (PyDict_Next(kwargs, &pos, &key, &value))
			lru_ass_sub(self, key, value);
	}

	Py_RETURN_NONE;
}

static PyObject *
LRU_setdefault(LRU *self, PyObject *args)
{
    PyObject *key;
    PyObject *default_obj = NULL;
    PyObject *result;

    if (!PyArg_ParseTuple(args, "O|O", &key, &default_obj))
        return NULL;

    result = lru_subscript(self, key);
    PyErr_Clear();
    if (result)
        return result;

    if (!default_obj)
        default_obj = Py_None;

    if (lru_ass_sub(self, key, default_obj) != 0)
        return NULL;

    Py_INCREF(default_obj);
    return default_obj;
}

static PyObject *
LRU_pop(LRU *self, PyObject *args)
{
    PyObject *key;
    PyObject *default_obj = NULL;
    PyObject *result;

    if (!PyArg_ParseTuple(args, "O|O", &key, &default_obj))
        return NULL;

    /* Trying to access the item by key. */
    result = lru_subscript(self, key);

    if (result)
        /* result != NULL, delete it from dict by key */
        lru_ass_sub(self, key, NULL);
    else if (default_obj) {
        /* result == NULL, i.e. key missing, and default_obj given */
        PyErr_Clear();
        Py_INCREF(default_obj);
        result = default_obj;
    }
    /* Otherwise (key missing, and default_obj not given [i.e. == NULL]), the
     * call to lru_subscript (at the location marked by "Trying to access the
     * item by key" in the comments) has already generated the appropriate
     * exception. */

    return result;
}

static PyObject *
LRU_peek_first_item(LRU *self)
{
    Node *node, *need_delete;
    _PyTime_t t_now;

    t_now = _PyTime_GetSystemClock();

    node = self->first;
    while (node) {
        if (IS_EXPIRED(t_now, node)){
            need_delete = node;
            node = node->next;
            lru_remove_node(self, need_delete);
            PUT_NODE(self->dict, need_delete->key, NULL);
        } else {
            return get_item(node);
        }
    }
    Py_RETURN_NONE;
}

static PyObject *
LRU_peek_last_item(LRU *self)
{
    Node *node, *need_delete;
    _PyTime_t t_now;

    t_now = _PyTime_GetSystemClock();

    node = self->last;
    while (node) {
        if (IS_EXPIRED(t_now, node)){
            need_delete = node;
            node = node->prev;
            lru_remove_node(self, need_delete);
            PUT_NODE(self->dict, need_delete->key, NULL);
        } else {
            return get_item(node);
        }
    }
    Py_RETURN_NONE;
}

static PyObject *
LRU_popitem(LRU *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"least_recent", NULL};
    int pop_least_recent = 1;
    PyObject *result;

#if PY_MAJOR_VERSION >= 3
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|p", kwlist, &pop_least_recent))
        return NULL;
#else
    {
        PyObject *arg_ob = Py_True;
        if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O", kwlist, &arg_ob))
            return NULL;
        pop_least_recent = PyObject_IsTrue(arg_ob);
        if (pop_least_recent == -1)
            return NULL;
    }
#endif
    if (pop_least_recent)
        result = LRU_peek_last_item(self);
    else
        result = LRU_peek_first_item(self);
    if (result == Py_None) {
        PyErr_SetString(PyExc_KeyError, "popitem(): LRU dict is empty");
        return NULL;
    }
    lru_ass_sub(self, PyTuple_GET_ITEM(result, 0), NULL);
    Py_INCREF(result);
    return result;
}

static PyObject *
LRU_keys(LRU *self) {
    return collect(self, get_key);
}

static PyObject *
get_value(Node *node)
{
    Py_INCREF(node->value);
    return node->value;
}

static PyObject *
LRU_values(LRU *self)
{
    return collect(self, get_value);
}

static PyObject *
LRU_set_callback(LRU *self, PyObject *args)
{
    return set_callback(self, args);
}



static PyObject *
LRU_items(LRU *self)
{
    return collect(self, get_item);
}

static PyObject *
LRU_set_size(LRU *self, PyObject *args, PyObject *kwds)
{
    Py_ssize_t newSize;
    if (!PyArg_ParseTuple(args, "n", &newSize)) {
        return NULL;
    }
    if (newSize <= 0) {
        PyErr_SetString(PyExc_ValueError, "Size should be a positive number");
        return NULL;
    }
    while (lru_length(self) > newSize) {
        lru_delete_last(self);
    }
    self->size = newSize;
    Py_RETURN_NONE;
}

static PyObject *
LRU_clear(LRU *self)
{
    Node *c = self->first;

    while (c) {
        Node* n = c;
        c = c->next;
        lru_remove_node(self, n);
    }
    PyDict_Clear(self->dict);

    self->hits = 0;
    self->misses = 0;
    Py_RETURN_NONE;
}


static PyObject *
LRU_get_size(LRU *self)
{
    return Py_BuildValue("i", self->size);
}

static PyObject *
LRU_get_stats(LRU *self)
{
    return Py_BuildValue("nn", self->hits, self->misses);
}


/* Hack to implement "key in lru" */
static PySequenceMethods lru_as_sequence = {
    0,                             /* sq_length */
    0,                             /* sq_concat */
    0,                             /* sq_repeat */
    0,                             /* sq_item */
    0,                             /* sq_slice */
    0,                             /* sq_ass_item */
    0,                             /* sq_ass_slice */
    (objobjproc) LRU_seq_contains, /* sq_contains */
    0,                             /* sq_inplace_concat */
    0,                             /* sq_inplace_repeat */
};

static PyMethodDef LRU_methods[] = {
    {"__contains__", (PyCFunction)LRU_contains_key, METH_O | METH_COEXIST,
                    PyDoc_STR("L.__contains__(key) -> Check if key is there in L")},
    {"keys", (PyCFunction)LRU_keys, METH_NOARGS,
                    PyDoc_STR("L.keys() -> list of L's keys in MRU order")},
    {"values", (PyCFunction)LRU_values, METH_NOARGS,
                    PyDoc_STR("L.values() -> list of L's values in MRU order")},
    {"items", (PyCFunction)LRU_items, METH_NOARGS,
                    PyDoc_STR("L.items() -> list of L's items (key,value) in MRU order")},
    {"has_key",	(PyCFunction)LRU_contains, METH_VARARGS,
                    PyDoc_STR("L.has_key(key) -> Check if key is there in L")},
    {"set_with_ttl", (PyCFunction)LRU_set_with_ttl, METH_VARARGS,
                    PyDoc_STR("L.set_with_ttl(key, value, ttl) -> Set key to value with a ttl")},                
    {"get",	(PyCFunction)LRU_get, METH_VARARGS,
                    PyDoc_STR("L.get(key, instead) -> If L has key return its value, otherwise instead")},
    {"setdefault", (PyCFunction)LRU_setdefault, METH_VARARGS,
                    PyDoc_STR("L.setdefault(key, default=None) -> If L has key return its value, otherwise insert key with a value of default and return default")},
    {"pop", (PyCFunction)LRU_pop, METH_VARARGS,
                    PyDoc_STR("L.pop(key[, default]) -> If L has key return its value and remove it from L, otherwise return default. If default is not given and key is not in L, a KeyError is raised.")},
    {"popitem", (PyCFunctionWithKeywords)LRU_popitem, METH_VARARGS | METH_KEYWORDS,
                    PyDoc_STR("L.popitem([least_recent=True]) -> Returns and removes a (key, value) pair. The pair returned is the least-recently used if least_recent is true, or the most-recently used if false.")},
    {"set_size", (PyCFunction)LRU_set_size, METH_VARARGS,
                    PyDoc_STR("L.set_size() -> set size of LRU")},
    {"get_size", (PyCFunction)LRU_get_size, METH_NOARGS,
                    PyDoc_STR("L.get_size() -> get size of LRU")},
    {"clear", (PyCFunction)LRU_clear, METH_NOARGS,
                    PyDoc_STR("L.clear() -> clear LRU")},
    {"get_stats", (PyCFunction)LRU_get_stats, METH_NOARGS,
                    PyDoc_STR("L.get_stats() -> returns a tuple with cache hits and misses")},
    {"peek_first_item", (PyCFunction)LRU_peek_first_item, METH_NOARGS,
                    PyDoc_STR("L.peek_first_item() -> returns the MRU item (key,value) without changing key order")},
    {"peek_last_item", (PyCFunction)LRU_peek_last_item, METH_NOARGS,
                    PyDoc_STR("L.peek_last_item() -> returns the LRU item (key,value) without changing key order")},
    {"update", (PyCFunction)LRU_update, METH_VARARGS | METH_KEYWORDS,
                    PyDoc_STR("L.update() -> update value for key in LRU")},
    {"set_callback", (PyCFunction)LRU_set_callback, METH_VARARGS,
                    PyDoc_STR("L.set_callback(callback) -> set a callback to call when an item is evicted.")},
    {NULL,	NULL},
};

static PyObject*
LRU_repr(LRU* self)
{
    return PyObject_Repr(self->dict);
}

static int
LRU_init(LRU *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"size", "callback", "ttl", NULL};
    PyObject *callback = NULL;
    self->callback = NULL;
    self->default_ttl = -1;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "n|OL", kwlist, &self->size, &callback, &self->default_ttl)) {
        return -1;
    }

    if (callback && callback != Py_None) {
        if (!PyCallable_Check(callback)) {
            PyErr_SetString(PyExc_TypeError, "parameter must be callable");
            return -1;
        }
        Py_XINCREF(callback);
        self->callback = callback;
    }

    if ((Py_ssize_t)self->size <= 0) {
        PyErr_SetString(PyExc_ValueError, "Size should be a positive number");
        return -1;
    }
    self->dict = PyDict_New();
    self->first = self->last = NULL;
    self->hits = 0;
    self->misses = 0;
    return 0;
}

static void
LRU_dealloc(LRU *self)
{
    if (self->dict) {
        LRU_clear(self);
        Py_DECREF(self->dict);
        Py_XDECREF(self->callback);
    }
    PyObject_Del((PyObject*)self);
}

PyDoc_STRVAR(lru_doc,
"TTLRU(size, callback=None, ttl=1e9) -> new TTLRU dict that can store up to size elements\n"
"A TTLRU dict behaves like a standard dict, except that it stores only fixed\n"
"set of elements. Once the size overflows, it evicts least recently used\n"
"items.  If a callback is set it will call the callback with the evicted key\n"
" and item.\n\n"
"Eg:\n"
">>> l = TTLRU(3)\n"
">>> for i in range(5):\n"
">>>   l[i] = str(i)\n"
">>> l.keys()\n"
"[2,3,4]\n\n"
"Note: A TTLRU(n) can be thought of as a dict that will have the most\n"
"recently accessed n items.\n");

static PyTypeObject LRUType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "ttlru.TTLRU",           /* tp_name */
    sizeof(LRU),             /* tp_basicsize */
    0,                       /* tp_itemsize */
    (destructor)LRU_dealloc, /* tp_dealloc */
    0,                       /* tp_print */
    0,                       /* tp_getattr */
    0,                       /* tp_setattr */
    0,                       /* tp_compare */
    (reprfunc)LRU_repr,      /* tp_repr */
    0,                       /* tp_as_number */
    &lru_as_sequence,        /* tp_as_sequence */
    &LRU_as_mapping,         /* tp_as_mapping */
    0,                       /* tp_hash */
    0,                       /* tp_call */
    0,                       /* tp_str */
    0,                       /* tp_getattro */
    0,                       /* tp_setattro */
    0,                       /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,      /* tp_flags */
    lru_doc,                 /* tp_doc */
    0,                       /* tp_traverse */
    0,                       /* tp_clear */
    0,                       /* tp_richcompare */
    0,                       /* tp_weaklistoffset */
    0,                       /* tp_iter */
    0,                       /* tp_iternext */
    LRU_methods,             /* tp_methods */
    0,                       /* tp_members */
    0,                       /* tp_getset */
    0,                       /* tp_base */
    0,                       /* tp_dict */
    0,                       /* tp_descr_get */
    0,                       /* tp_descr_set */
    0,                       /* tp_dictoffset */
    (initproc)LRU_init,      /* tp_init */
    0,                       /* tp_alloc */
    0,                       /* tp_new */
};

#if PY_MAJOR_VERSION >= 3
  static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "ttlru",          /* m_name */
    lru_doc,          /* m_doc */
    -1,               /* m_size */
    NULL,             /* m_methods */
    NULL,             /* m_reload */
    NULL,             /* m_traverse */
    NULL,             /* m_clear */
    NULL,             /* m_free */
  };
#endif

static PyObject *
moduleinit(void)
{
    PyObject *m;

    NodeType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&NodeType) < 0)
        return NULL;

    LRUType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&LRUType) < 0)
        return NULL;

    #if PY_MAJOR_VERSION >= 3
        m = PyModule_Create(&moduledef);
    #else
        m = Py_InitModule3("ttlru", NULL, lru_doc);
    #endif

    if (m == NULL)
        return NULL;

    Py_INCREF(&NodeType);
    Py_INCREF(&LRUType);
    PyModule_AddObject(m, "TTLRU", (PyObject *) &LRUType);

    return m;
}

#if PY_MAJOR_VERSION < 3
    PyMODINIT_FUNC
    initlru(void)
    {
        moduleinit();
    }
#else
    PyMODINIT_FUNC
    PyInit_ttlru(void)
    {
        return moduleinit();
    }
#endif
