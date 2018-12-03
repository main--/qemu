/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include <getopt.h>
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qlist.h"
#include "libqos/malloc.h"
#include "libqos/qgraph.h"
#include "libqos/qgraph_internal.h"

static char *old_path;

/**
 * create_machine_name(): appends the architecture to @name if
 * @is_machine is valid.
 */
static void create_machine_name(const char **name, bool is_machine)
{
    const char *arch;
    if (!is_machine) {
        return;
    }
    arch = qtest_get_arch();
    *name = g_strconcat(arch, "/", *name, NULL);
}

/**
 * destroy_machine_name(): frees the given @name if
 * @is_machine is valid.
 */
static void destroy_machine_name(const char *name, bool is_machine)
{
    if (!is_machine) {
        return;
    }
    g_free((char *)name);
}

/**
 * apply_to_qlist(): using QMP queries QEMU for a list of
 * machines and devices available, and sets the respective node
 * as TRUE. If a node is found, also all its produced and contained
 * child are marked available.
 *
 * See qos_graph_node_set_availability() for more info
 */
static void apply_to_qlist(QList *list, bool is_machine)
{
    const QListEntry *p;
    const char *name;
    bool abstract;
    QDict *minfo;
    QObject *qobj;
    QString *qstr;
    QBool *qbol;

    for (p = qlist_first(list); p; p = qlist_next(p)) {
        minfo = qobject_to(QDict, qlist_entry_obj(p));
        qobj = qdict_get(minfo, "name");
        qstr = qobject_to(QString, qobj);
        name = qstring_get_str(qstr);

        create_machine_name(&name, is_machine);
        qos_graph_node_set_availability(name, TRUE);

        qobj = qdict_get(minfo, "alias");
        if (qobj) {
            qstr = qobject_to(QString, qobj);

            destroy_machine_name(name, is_machine);
            name = qstring_get_str(qstr);

            create_machine_name(&name, is_machine);
            qos_graph_node_set_availability(name, TRUE);
        }

        qobj = qdict_get(minfo, "abstract");
        if (qobj) {
            qbol = qobject_to(QBool, qobj);
            abstract = qbool_get_bool(qbol);
            qos_delete_abstract_cmd_line(name, abstract);
        }

        destroy_machine_name(name, is_machine);
    }
}

/**
 * qos_set_machines_devices_available(): sets availability of qgraph
 * machines and devices.
 *
 * This function firstly starts QEMU with "-machine none" option,
 * and then executes the QMP protocol asking for the list of devices
 * and machines available.
 *
 * for each of these items, it looks up the corresponding qgraph node,
 * setting it as available. The list currently returns all devices that
 * are either machines or QEDGE_CONSUMED_BY other nodes.
 * Therefore, in order to mark all other nodes, it recursively sets
 * all its QEDGE_CONTAINS and QEDGE_PRODUCES child as available too.
 */
static void qos_set_machines_devices_available(void)
{
    QDict *response;
    QDict *args = qdict_new();
    QList *list;

    qtest_start("-machine none");
    response = qmp("{ 'execute': 'query-machines' }");
    list = qdict_get_qlist(response, "return");

    apply_to_qlist(list, TRUE);

    qobject_unref(response);

    qdict_put_bool(args, "abstract", TRUE);
    qdict_put_str(args, "implements", "device");

    response = qmp("{'execute': 'qom-list-types',"
                   " 'arguments': %p }", args);
    g_assert(qdict_haskey(response, "return"));
    list = qdict_get_qlist(response, "return");

    apply_to_qlist(list, FALSE);

    qtest_end();
    qobject_unref(response);

}

static QGuestAllocator *get_machine_allocator(QOSGraphObject *obj)
{
    if (obj->get_driver) {
        return obj->get_driver(obj, "memory");
    } else {
        return NULL;
    }
}

static void restart_qemu_or_continue(char *path)
{
    /* compares the current command line with the
     * one previously executed: if they are the same,
     * don't restart QEMU, if they differ, stop previous
     * QEMU subprocess (if active) and start over with
     * the new command line
     */
    if (g_strcmp0(old_path, path)) {
        qtest_end();
        qos_invalidate_command_line();
        old_path = g_strdup(path);
        qtest_start(path);
    } else { /* if cmd line is the same, reset the guest */
        qobject_unref(qmp("{ 'execute': 'system_reset' }"));
        qmp_eventwait("RESET");
    }
}

void qos_invalidate_command_line(void)
{
    g_free(old_path);
    old_path = NULL;
}

/**
 * allocate_objects(): given an array of nodes @arg,
 * walks the path invoking all constructors and
 * passing the corresponding parameter in order to
 * continue the objects allocation.
 * Once the test is reached, return the object it consumes.
 *
 * Since the machine and QEDGE_CONSUMED_BY nodes allocate
 * memory in the constructor, g_test_queue_destroy is used so
 * that after execution they can be safely free'd.  (The test's
 * ->before callback is also welcome to use g_test_queue_destroy).
 *
 * Note: as specified in walk_path() too, @arg is an array of
 * char *, where arg[0] is a pointer to the command line
 * string that will be used to properly start QEMU when executing
 * the test, and the remaining elements represent the actual objects
 * that will be allocated.
 */
static void *allocate_objects(QTestState *qts, char **path, QGuestAllocator **p_alloc)
{
    int current = 0;
    QGuestAllocator *alloc;
    QOSGraphObject *parent = NULL;
    QOSGraphEdge *edge;
    QOSGraphNode *node;
    void *edge_arg;
    void *obj;

    node = qos_graph_get_node(path[current]);
    g_assert(node->type == QNODE_MACHINE);

    obj = qos_machine_new(node, qts);
    qos_object_queue_destroy(obj);

    alloc = get_machine_allocator(obj);
    if (p_alloc) {
        *p_alloc = alloc;
    }

    for (;;) {
        if (node->type != QNODE_INTERFACE) {
            qos_object_start_hw(obj);
            parent = obj;
        }

        /* follow edge and get object for next node constructor */
        current++;
        edge = qos_graph_get_edge(path[current - 1], path[current]);
        node = qos_graph_get_node(path[current]);

        if (node->type == QNODE_TEST) {
            g_assert(qos_graph_edge_get_type(edge) == QEDGE_CONSUMED_BY);
            return obj;
        }

        switch (qos_graph_edge_get_type(edge)) {
        case QEDGE_PRODUCES:
            obj = parent->get_driver(parent, path[current]);
            break;

        case QEDGE_CONSUMED_BY:
            edge_arg = qos_graph_edge_get_arg(edge);
            obj = qos_driver_new(node, obj, alloc, edge_arg);
            qos_object_queue_destroy(obj);
            break;

        case QEDGE_CONTAINS:
            obj = parent->get_device(parent, path[current]);
            break;
        }
    }
}

/* The argument to run_one_test, which is the test function that is registered
 * with GTest, is a vector of strings.  The first item is the initial command
 * line (before it is modified by the test's "before" function), the remaining
 * items are node names forming the path to the test node.
 */
static char **current_path;

const char *qos_get_current_command_line(void)
{
    return current_path[0];
}

void *qos_allocate_objects(QTestState *qts, QGuestAllocator **p_alloc)
{
    return allocate_objects(qts, current_path + 1, p_alloc);
}

/**
 * run_one_test(): given an array of nodes @arg,
 * walks the path invoking all constructors and
 * passing the corresponding parameter in order to
 * continue the objects allocation.
 * Once the test is reached, its function is executed.
 *
 * Since the machine and QEDGE_CONSUMED_BY nodes allocate
 * memory in the constructor, g_test_queue_destroy is used so
 * that after execution they can be safely free'd.  The test's
 * ->before callback is also welcome to use g_test_queue_destroy.
 *
 * Note: as specified in walk_path() too, @arg is an array of
 * char *, where arg[0] is a pointer to the command line
 * string that will be used to properly start QEMU when executing
 * the test, and the remaining elements represent the actual objects
 * that will be allocated.
 *
 * The order of execution is the following:
 * 1) @before test function as defined in the given QOSGraphTestOptions
 * 2) start QEMU
 * 3) call all nodes constructor and get_driver/get_device depending on edge,
 *    start the hardware (*_device_enable functions)
 * 4) start test
 */
static void run_one_test(const void *arg)
{
    QOSGraphNode *test_node;
    QGuestAllocator *alloc = NULL;
    void *obj;
    char **path = (char **) arg;
    GString *cmd_line = g_string_new(path[0]);
    void *test_arg;

    /* Before test */
    current_path = path;
    test_node = qos_graph_get_node(path[(g_strv_length(path) - 1)]);
    test_arg = test_node->u.test.arg;
    if (test_node->u.test.before) {
        test_arg = test_node->u.test.before(cmd_line, test_arg);
    }

    restart_qemu_or_continue(cmd_line->str);
    g_string_free(cmd_line, TRUE);

    obj = qos_allocate_objects(global_qtest, &alloc);
    test_node->u.test.function(obj, test_arg, alloc);
}

static void subprocess_run_one_test(const void *arg)
{
    const gchar *path = arg;
    g_test_trap_subprocess(path, 0, 0);
    g_test_trap_assert_passed();
}

/*
 * in this function, 2 path will be built:
 * path_str, a one-string path (ex "pc/i440FX-pcihost/...")
 * path_vec, a string-array path (ex [0] = "pc", [1] = "i440FX-pcihost").
 *
 * path_str will be only used to build the test name, and won't need the
 * architecture name at beginning, since it will be added by qtest_add_func().
 *
 * path_vec is used to allocate all constructors of the path nodes.
 * Each name in this array except position 0 must correspond to a valid
 * QOSGraphNode name.
 * Position 0 is special, initially contains just the <machine> name of
 * the node, (ex for "x86_64/pc" it will be "pc"), used to build the test
 * path (see below). After it will contain the command line used to start
 * qemu with all required devices.
 *
 * Note that the machine node name must be with format <arch>/<machine>
 * (ex "x86_64/pc"), because it will identify the node "x86_64/pc"
 * and start QEMU with "-M pc". For this reason,
 * when building path_str, path_vec
 * initially contains the <machine> at position 0 ("pc"),
 * and the node name at position 1 (<arch>/<machine>)
 * ("x86_64/pc"), followed by the rest of the nodes.
 */
static void walk_path(QOSGraphNode *orig_path, int len)
{
    QOSGraphNode *path;
    QOSGraphEdge *edge;

    /* etype set to QEDGE_CONSUMED_BY so that machine can add to the command line */
    QOSEdgeType etype = QEDGE_CONSUMED_BY;

    /* twice QOS_PATH_MAX_ELEMENT_SIZE since each edge can have its arg */
    char **path_vec = g_new0(char *, (QOS_PATH_MAX_ELEMENT_SIZE * 2));
    int path_vec_size = 0;

    char *machine = NULL, *arch = NULL;
    char *after_cmd = NULL, *before_cmd = NULL, *after_device = NULL;
    char *node_name = orig_path->name, *path_str;

    GString *cmd_line = g_string_new("");
    GString *cmd_line2 = g_string_new("");

    path = qos_graph_get_node(node_name); /* root */
    node_name = qos_graph_edge_get_dest(path->path_edge); /* machine name */

    qos_separate_arch_machine(node_name, &arch, &machine);
    path_vec[path_vec_size++] = arch;
    path_vec[path_vec_size++] = machine;

    for (;;) {
        path = qos_graph_get_node(node_name);
        if (!path->path_edge) {
            break;
        }

        node_name = qos_graph_edge_get_dest(path->path_edge);

        /* append node command line + previous edge command line */
        if (path->command_line && etype == QEDGE_CONSUMED_BY) {
            g_string_append(cmd_line, path->command_line);
            if (after_device) {
                g_string_append(cmd_line, after_device);
            }
        }

        path_vec[path_vec_size++] = qos_graph_edge_get_name(path->path_edge);
        /* detect if edge has command line args */
        after_cmd = qos_graph_edge_get_after_cmd_line(path->path_edge);
        after_device = qos_graph_edge_get_extra_device_opts(path->path_edge);
        before_cmd = qos_graph_edge_get_before_cmd_line(path->path_edge);
        edge = qos_graph_get_edge(path->name, node_name);
        etype = qos_graph_edge_get_type(edge);

        if (before_cmd) {
            g_string_append(cmd_line, before_cmd);
        }
        if (after_cmd) {
            g_string_append(cmd_line2, after_cmd);
        }
    }

    path_vec[path_vec_size++] = NULL;
    if (after_device) {
        g_string_append(cmd_line, after_device);
    }
    g_string_append(cmd_line, cmd_line2->str);
    g_string_free(cmd_line2, TRUE);

    /* here position 0 has <arch>/<machine>, position 1 has <machine>.
     * The path must not have the <arch>
     */
    path_str = g_strjoinv("/", path_vec + 1);

    /* put arch/machine in position 1 so run_one_test can do its work
     * and add the command line at position 0.
     */
    path_vec[0] = g_string_free(cmd_line, FALSE);
    path_vec[1] = arch;

    if (path->u.test.subprocess) {
        gchar *subprocess_path = g_strdup_printf("/%s/%s/subprocess",
                                                 qtest_get_arch(), path_str);
        qtest_add_data_func(path_str, subprocess_path, subprocess_run_one_test);
        g_test_add_data_func(subprocess_path, path_vec, run_one_test);
    } else {
        qtest_add_data_func(path_str, path_vec, run_one_test);
    }

    g_free(path_str);
}



/**
 * main(): heart of the qgraph framework.
 *
 * - Initializes the glib test framework
 * - Creates the graph by invoking the various _init constructors
 * - Starts QEMU to mark the available devices
 * - Walks the graph, and each path is added to
 *   the glib test framework (walk_path)
 * - Runs the tests, calling allocate_object() and allocating the
 *   machine/drivers/test objects
 * - Cleans up everything
 */
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qos_graph_init();
    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_LIBQOS);
    qos_set_machines_devices_available();

    qos_graph_foreach_test_path(walk_path);
    g_test_run();
    qtest_end();
    qos_graph_destroy();
    g_free(old_path);
    return 0;
}
