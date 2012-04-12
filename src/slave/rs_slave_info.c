
#include <rs_config.h>
#include <rs_core.h>
#include <rs_slave.h>

static int rs_init_slave_conf(rs_slave_info_t *mi);
static int rs_parse_slave_info(rs_slave_info_t *s);

rs_slave_info_t *rs_init_slave_info(rs_slave_info_t *os) 
{
    int                 nr, ni, nrb, err;
    rs_slave_info_t     *si;

    si = (rs_slave_info_t *) malloc(sizeof(rs_slave_info_t));
    nr = 1;
    ni = 1;
    nrb = 1;

    if(si == NULL) {
        rs_log_err(rs_errno, "malloc() failed, rs_slave_info_t");
        goto free;
    }

    rs_slave_info_t_init(si);

    /* init conf */
    if(rs_init_slave_conf(si) != RS_OK) {
        rs_log_err(0, "slave conf init failed");
        goto free;
    }

    /* init thread attr */
    if((err = pthread_attr_init(&(si->thread_attr))) != 0) {
        rs_log_err(err, "pthread_attr_init() failed, thread_attr");
        goto free;
    }

    /* set thread detached */
    if((err = pthread_attr_setdetachstate(&(si->thread_attr), 
                    PTHREAD_CREATE_DETACHED)) != 0) 
    {
        rs_log_err(err, "pthread_attr_setdetachstate() failed, DETACHED");
        goto free;
    }

    /* slave info */
    si->info_fd = open(si->slave_info, O_CREAT | O_RDWR, 00666);

    if(si->info_fd == -1) {
        rs_log_err(rs_errno, "open(\"%s\") failed", si->slave_info);
        goto free;
    }

    /* parse slave.info */
    if(rs_parse_slave_info(si) != RS_OK) {
        goto free;
    }

    if(os != NULL) {

        /* dump file or pos */
        nrb = (os->dump_pos != si->dump_pos || 
                        rs_strcmp(os->dump_file, si->dump_file) != 0);

        /* redis addr or port */
        nr = (os->redis_port != si->redis_port || 
                rs_strcmp(os->redis_addr, si->redis_addr) != 0) | nrb;

        /* listen addr or port */
        ni = (os->listen_port != si->listen_port || 
                rs_strcmp(os->listen_addr, si->listen_addr) != 0) 
            | nrb;

        if(ni) {

            if(os->io_thread != 0) {

                rs_log_info("start exiting io thread");

                if((err = pthread_cancel(os->io_thread)) != 0) {
                    rs_log_err(err, "pthread_cancel() failed, io_thread");
                }
            }

            rs_close(os->svr_fd);
        }

        if(nr) {

            if(os->redis_thread != 0) {

                rs_log_info("start exiting redis thread");

                if((err = pthread_cancel(os->redis_thread)) != 0) {
                    rs_log_err(err, "pthread_cancel() failed, redis_thread");
                }
            }
        }

        if(!nrb) {
            si->ring_buf = os->ring_buf;
        }

    }

    if(nrb) {
        si->ring_buf = (rs_ring_buffer_t *) malloc(sizeof(rs_ring_buffer_t));
        
        if(si->ring_buf == NULL) {
            rs_log_err(rs_errno, "malloc() failed, rs_ring_buffer_t");
        }

        /* init ring buffer for io and redis thread */
        if(rs_init_ring_buffer(si->ring_buf,  RS_SYNC_DATA_SIZE, 
                    2046) != RS_OK) {
        }
    }

    if(ni) {
        rs_log_info("io thread start");

        /* init io thread */
        if((err = pthread_create(&(si->io_thread), &(si->thread_attr), 
                        rs_start_io_thread, (void *) si)) != 0) 
        {
            rs_log_err(err, "pthread_create() failed, io_thread");
            goto free;
        }
    }

    if(nr) {
        rs_log_info("redis thread start");

        /* init redis thread */
        if((err = pthread_create(&(si->redis_thread), &(si->thread_attr), 
                        rs_start_redis_thread, (void *) si)) != 0) 
        {
            rs_log_err(err, "pthread_create() failed redis_thread");
            goto free;
        }
    }

    /* free old slave info */
    if(os != NULL) {
        if(nrb) {
            rs_free_ring_buffer(os->ring_buf);
        }

        free(os->ring_buf);
        free(os->conf);
        free(os);
    }

    return si;

free:

    /* free new slave info */
    rs_free_slave(si);

    /* rollback */
    if(os != NULL) {
        if(ni) {
            rs_log_info("rollback old slave info");

            rs_log_info("io thread start");

            /* init io thread */
            if((err = pthread_create(&(si->io_thread), &(si->thread_attr), 
                            rs_start_io_thread, (void *) si)) != 0) 
            {
                rs_log_err(err, "pthread_create() failed, io_thread");
                goto free;
            }
        }

        if(nr) {
            rs_log_info("redis thread start");

            /* init redis thread */
            if((err = pthread_create(&(si->redis_thread), &(si->thread_attr), 
                            rs_start_redis_thread, (void *) si)) != 0) 
            {
                rs_log_err(err, "pthread_create() failed redis_thread");
                goto free;
            }
        }
    }

    return NULL;
}

static int rs_init_slave_conf(rs_slave_info_t *si)
{

    rs_conf_kv_t *slave_conf;

    slave_conf = (rs_conf_kv_t *) malloc(sizeof(rs_conf_kv_t) * 
            RS_SLAVE_CONF_NUM);

    if(slave_conf == NULL) {
        rs_log_err(rs_errno, "malloc() failed, rs_conf_kv_t * slave_num");
        return RS_ERR;
    }

    si->conf = slave_conf;

    rs_str_set(&(slave_conf[0].k), "listen.addr");
    rs_conf_v_set(&(slave_conf[0].v), si->listen_addr, RS_CONF_STR);

    rs_str_set(&(slave_conf[1].k), "listen.port");
    rs_conf_v_set(&(slave_conf[1].v), si->listen_port, RS_CONF_INT32);

    rs_str_set(&(slave_conf[2].k), "slave.info");
    rs_conf_v_set(&(slave_conf[2].v), si->slave_info, RS_CONF_STR);

    rs_str_set(&(slave_conf[3].k), "redis.addr");
    rs_conf_v_set(&(slave_conf[3].v), si->redis_addr, RS_CONF_STR);

    rs_str_set(&(slave_conf[4].k), "redis.port");
    rs_conf_v_set(&(slave_conf[4].v), si->redis_port, RS_CONF_INT32);

    rs_str_set(&(slave_conf[5].k), NULL);
    rs_conf_v_set(&(slave_conf[5].v), "", RS_CONF_NULL);

    /* init master conf */
    if(rs_init_conf(rs_conf_path, RS_SLAVE_MODULE_NAME, slave_conf) != RS_OK) {
        rs_log_err(0, "slave conf init failed");
        return RS_ERR;
    }

    return RS_OK;
}

static int rs_parse_slave_info(rs_slave_info_t *si) 
{
    char    *p, buf[RS_SLAVE_INFO_STR_LEN];
    ssize_t n;

    /* binlog path, binlog pos  */
    p = buf;

    n = rs_read(si->info_fd, buf, RS_SLAVE_INFO_STR_LEN);

    if(n < 0) {
        rs_log_debug(0, "rs_read() in rs_parse_slave_info failed");
        return RS_ERR;
    } else if (n > 0) { 
        buf[n] = '\0';
        p = rs_ncp_str_till(si->dump_file, p, ',', PATH_MAX);
        si->dump_pos = rs_str_to_uint32(p);
    } else {
        /* if a new slave.info file */
        rs_log_err(0, "slave.info is empty, "
                "must specified format like \"binlog_file,binlog_pos\"");
        return RS_ERR;
    }

    rs_log_info("parse slave info binlog file = %s, pos = %u",
            si->dump_file, si->dump_pos);

    return RS_OK;
}

/*
 *  rs_flush_slave_info
 *  @s:rs_slave_info_s struct
 *
 *  Flush slave into to disk, format like rsylog_path,rsylog_pos
 *
 *  On success, RS_OK is returned. On error, RS_ERR is returned
 */
int rs_flush_slave_info(rs_slave_info_t *si, char *buf, size_t len) 
{
    ssize_t n;

    rs_log_info("flush slave.info buf = %*.*s", len, len, buf);

    if(lseek(si->info_fd, 0, SEEK_SET) == -1) {
        rs_log_err(rs_errno, "lseek(\"%s\") failed", si->slave_info);
        return RS_ERR;
    }

    n = rs_write(si->info_fd, buf, len);

    if((size_t) n != len) {
        return RS_ERR;
    } 

    if(fdatasync(si->info_fd) != 0) {
        rs_log_err(rs_errno, "fdatasync(\"%s\") failed", si->slave_info);
        return RS_ERR;
    }

    /* truncate other remained file bytes */
    if(truncate(si->slave_info, len) != 0) {
        rs_log_err(rs_errno, "truncate(\"%s\", %d) failed", 
                si->slave_info, len);
        return RS_ERR;
    }

    return RS_OK; 
}