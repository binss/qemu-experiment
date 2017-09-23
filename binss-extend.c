#include "qemu/osdep.h"

#include "qemu-common.h"
#include "migration/migration.h"
#include "migration/vmstate.h"
#include "qemu/coroutine.h"
#include "io/channel-file.h"


#include "sysemu/sysemu.h"
#include "monitor/monitor.h"

#include "qapi/error.h"
#include "qemu/error-report.h"

// channal
#include "io/channel-socket.h"
#include "io/channel-util.h"
#include "io-channel-helpers.h"

#include "migration/qemu-file.h"




#include "binss-extend.h"

void test_io_channel_recv(Monitor *mon)
{
    QIOChannel *dst;
    SocketAddress *listen_addr;
    QIOChannelSocket *lioc;
    char bufsend[12];
    // struct iovec iosend[1];
    QEMUFile *f;

    listen_addr = g_new0(SocketAddress, 1);
    listen_addr->type = SOCKET_ADDRESS_KIND_INET;
    listen_addr->u.inet.data = g_new(InetSocketAddress, 1);
    *listen_addr->u.inet.data = (InetSocketAddress) {
        .host = g_strdup("127.0.0.1"),
        .port = g_strdup("12345"), /* NULL == Auto-select */
    };

    lioc = qio_channel_socket_new();

    monitor_printf(mon, "Listening...\n");
    qio_channel_socket_listen_sync(lioc, listen_addr, &error_abort);

    monitor_printf(mon, "Waiting...\n");
    qio_channel_wait(QIO_CHANNEL(lioc), G_IO_IN);

    monitor_printf(mon, "Accepting...\n");
    dst = QIO_CHANNEL(qio_channel_socket_accept(lioc, &error_abort));
    g_assert(dst);


    monitor_printf(mon, "Writing...\n");
    memcpy(bufsend, "Hello World", G_N_ELEMENTS(bufsend));
    // ret = qio_channel_write(dst, bufsend, G_N_ELEMENTS(bufsend), &error_abort);

    f = qemu_fopen_channel_output(dst);
    qemu_put_buffer(f, (void *)bufsend, G_N_ELEMENTS(bufsend));
    qemu_fflush(f);
    // object_unref(OBJECT(lioc));
    // qapi_free_SocketAddress(listen_addr);
}

void test_io_channel_send(Monitor *mon)
{

    QIOChannel *src;

    SocketAddress *connect_addr;
    // struct iovec iorecv[1];
    char bufrecv[12];
    QEMUFile *f;



    connect_addr = g_new0(SocketAddress, 1);
    connect_addr->type = SOCKET_ADDRESS_KIND_INET;
    connect_addr->u.inet.data = g_new(InetSocketAddress, 1);
    *connect_addr->u.inet.data = (InetSocketAddress) {
        .host = g_strdup("127.0.0.1"),
        .port = g_strdup("12345"),
    };

    src = QIO_CHANNEL(qio_channel_socket_new());

    monitor_printf(mon, "Connecting...\n");
    qio_channel_socket_connect_sync(
        QIO_CHANNEL_SOCKET(src), connect_addr, &error_abort);
    qio_channel_set_delay(src, false);

    // ret = qio_channel_read(src, bufrecv, G_N_ELEMENTS(bufrecv), &error_abort);

    monitor_printf(mon, "Reading...\n");

    f = qemu_fopen_channel_input(src);
    memset(bufrecv, 0, G_N_ELEMENTS(bufrecv));
    qemu_get_buffer(f, (void *)bufrecv, G_N_ELEMENTS(bufrecv));

    monitor_printf(mon, "get: %s", bufrecv);
    // fprintf(stdout, "%s\n", bufrecv);
}


static int qemu_savevm_state_hack(QEMUFile *f, Error **errp)
{
    int ret;
    MigrationParams params = {
        .blk = 0,
        .shared = 0
    };
    MigrationState *ms = migrate_init(&params);
    // MigrationStatus status;
    ms->to_dst_file = f;

    if (migration_is_blocked(errp)) {
        ret = -EINVAL;
        goto done;
    }

    // 必须，否则会卡死
    qemu_mutex_unlock_iothread();
    // 一些全局变量(ram)依赖于该函数来初始化
    qemu_savevm_state_header(f);
    qemu_mutex_lock_iothread();

    qemu_savevm_cpu_state(f);

    ret = qemu_file_get_error(f);

done:
    return ret;
}


static int qemu_loadvm_state_hack(QEMUFile *f)
{
    return qemu_loadvm_cpu_state(f);
}

void write_file(Monitor *mon)
{
    if (!mon)
        return;

    int ret;
    QIOChannel *ioc;
    QEMUFile *f;
    uint64_t vm_state_size;
    Error *local_err = NULL;

    char temp_file[] = "/home/binss/work/cpu.save";
    int fd = open(temp_file, O_WRONLY | O_CREAT);

    ioc = QIO_CHANNEL(qio_channel_file_new_fd(fd));
    f = qemu_fopen_channel_output(ioc);
    // qemu_put_be32(f, 32);

    ret = qemu_savevm_state_hack(f, &local_err);

    if (ret < 0) {
        monitor_printf(mon, "Error\n");
    }

    vm_state_size = qemu_ftell(f);

    monitor_printf(mon, "vm_state_size: %"PRIu64"\n", vm_state_size);

    qemu_fclose(f);
}

void read_file(Monitor *mon)
{
    if (!mon)
        return;

    int ret;
    QIOChannel *ioc;
    QEMUFile *f;
    uint64_t vm_state_size;
    // Error *local_err = NULL;

    char temp_file[] = "/home/binss/work/cpu.save";
    // int fd = open(temp_file, O_RDONLY);
    int fd = open(temp_file, O_RDONLY);

    if (!fd) {
        monitor_printf(mon, "Could not open save file\n");
        return;
    }


    ioc = QIO_CHANNEL(qio_channel_file_new_fd(fd));
    f = qemu_fopen_channel_input(ioc);
    ret = qemu_loadvm_state_hack(f);

    vm_state_size = qemu_ftell(f);

    monitor_printf(mon, "vm_state_size: %"PRIu64"\n", vm_state_size);


    if (ret < 0) {
        monitor_printf(mon, "Error\n");
    }

    qemu_fclose(f);
}

