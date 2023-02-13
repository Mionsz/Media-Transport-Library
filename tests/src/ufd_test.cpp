/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2023 Intel Corporation
 */

#include "ufd_test.h"

#include <getopt.h>

#include "log.h"
#include "tests.h"

enum utest_args_cmd {
  UTEST_ARG_UNKNOWN = 0,
  UTEST_ARG_P_PORT = 0x100, /* start from end of ascii */
  UTEST_ARG_R_PORT,
  UTEST_ARG_LOG_LEVEL,
  UTEST_ARG_QUEUE_MODE,
};

static struct option utest_args_options[] = {
    {"p_port", required_argument, 0, UTEST_ARG_P_PORT},
    {"r_port", required_argument, 0, UTEST_ARG_R_PORT},
    {"log_level", required_argument, 0, UTEST_ARG_LOG_LEVEL},
    {"queue_mode", required_argument, 0, UTEST_ARG_QUEUE_MODE},
    {0, 0, 0, 0}};

static struct utest_ctx* g_utest_ctx;

struct utest_ctx* utest_get_ctx(void) {
  return g_utest_ctx;
}

static int utest_parse_args(struct utest_ctx* ctx, int argc, char** argv) {
  int cmd = -1, opt_idx = 0;
  struct mtl_init_params* p = &ctx->init_params.mt_params;

  while (1) {
    cmd = getopt_long_only(argc, argv, "hv", utest_args_options, &opt_idx);
    if (cmd == -1) break;
    dbg("%s, cmd %d %s\n", __func__, cmd, optarg);

    switch (cmd) {
      case UTEST_ARG_P_PORT:
        snprintf(p->port[MTL_PORT_P], sizeof(p->port[MTL_PORT_P]), "%s", optarg);
        p->num_ports++;
        break;
      case UTEST_ARG_R_PORT:
        snprintf(p->port[MTL_PORT_R], sizeof(p->port[MTL_PORT_R]), "%s", optarg);
        p->num_ports++;
        break;
      case UTEST_ARG_LOG_LEVEL:
        if (!strcmp(optarg, "debug"))
          p->log_level = MTL_LOG_LEVEL_DEBUG;
        else if (!strcmp(optarg, "info"))
          p->log_level = MTL_LOG_LEVEL_INFO;
        else if (!strcmp(optarg, "notice"))
          p->log_level = MTL_LOG_LEVEL_NOTICE;
        else if (!strcmp(optarg, "warning"))
          p->log_level = MTL_LOG_LEVEL_WARNING;
        else if (!strcmp(optarg, "error"))
          p->log_level = MTL_LOG_LEVEL_ERROR;
        else
          err("%s, unknow log level %s\n", __func__, optarg);
        break;
      case UTEST_ARG_QUEUE_MODE:
        if (!strcmp(optarg, "shared"))
          p->flags |= MTL_FLAG_SHARED_QUEUE;
        else if (!strcmp(optarg, "dedicated"))
          p->flags &= ~MTL_FLAG_SHARED_QUEUE;
        else
          err("%s, unknow queue mode %s\n", __func__, optarg);
        break;
      default:
        break;
    }
  };

  return 0;
}

static void utest_random_ip(struct utest_ctx* ctx) {
  struct mtl_init_params* p = &ctx->init_params.mt_params;
  uint8_t* p_ip = mtl_p_sip_addr(p);
  uint8_t* r_ip = mtl_r_sip_addr(p);

  srand(st_test_get_monotonic_time());

  p_ip[0] = 187;
  p_ip[1] = rand() % 0xFF;
  p_ip[2] = rand() % 0xFF;
  p_ip[3] = rand() % 0xFF;
  r_ip[0] = p_ip[0];
  r_ip[1] = p_ip[1];
  r_ip[2] = p_ip[2];
  r_ip[3] = p_ip[3] + 1;

  p_ip = ctx->mcast_ip_addr;
  p_ip[0] = 239;
  p_ip[1] = 187;
  p_ip[2] = rand() % 0xFF;
  p_ip[3] = rand() % 0xFF;
}

static void utest_ctx_init(struct utest_ctx* ctx) {
  struct mtl_init_params* p = &ctx->init_params.mt_params;

  memset(p, 0x0, sizeof(*p));

  p->flags |= MTL_FLAG_BIND_NUMA; /* default bind to numa */
  p->log_level = MTL_LOG_LEVEL_ERROR;
}

static void utest_ctx_uinit(struct utest_ctx* ctx) { st_test_free(ctx); }

static void socket_single_test(enum mtl_port port) {
  int ret = mufd_socket_port(AF_INET, SOCK_DGRAM, 0, port);
  EXPECT_GE(ret, 0);
  if (ret < 0) return;

  int fd = ret;
  ret = mufd_close(fd);
  EXPECT_GE(ret, 0);
}

TEST(Api, socket_single) { socket_single_test(MTL_PORT_P); }
TEST(Api, socket_single_r) { socket_single_test(MTL_PORT_R); }

static void socket_expect_fail_test(enum mtl_port port) {
  int ret;

  ret = mufd_socket_port(AF_INET6, SOCK_DGRAM, 0, port);
  EXPECT_LT(ret, 0);
  ret = mufd_socket_port(AF_INET, SOCK_STREAM, 0, port);
  EXPECT_LT(ret, 0);
}

TEST(Api, socket_expect_fail) {
  socket_expect_fail_test(MTL_PORT_P);
  int ret = mufd_socket_port(AF_INET, SOCK_STREAM, 0, MTL_PORT_MAX);
  EXPECT_LT(ret, 0);
}
TEST(Api, socket_expect_fail_r) { socket_expect_fail_test(MTL_PORT_R); }

static void socket_max_test(enum mtl_port port) {
  int ret;
  int max = mufd_get_sessions_max_nb();

  EXPECT_GT(max, 0);
  info("%s(%d), max %d\n", __func__, port, max);

  int fds[max];
  for (int i = 0; i < max; i++) {
    ret = mufd_socket_port(AF_INET, SOCK_DGRAM, 0, port);
    EXPECT_GE(ret, 0);
    fds[i] = ret;
  }

  /* all slots created, expect fail */
  ret = mufd_socket_port(AF_INET, SOCK_DGRAM, 0, port);
  EXPECT_LT(ret, 0);
  ret = mufd_socket_port(AF_INET, SOCK_DGRAM, 0, port);
  EXPECT_LT(ret, 0);

  for (int i = 0; i < max; i++) {
    ret = mufd_close(fds[i]);
    EXPECT_GE(ret, 0);
  }
}

TEST(Api, socket_max) { socket_max_test(MTL_PORT_P); }
TEST(Api, socket_max_r) { socket_max_test(MTL_PORT_R); }

template <typename OPT_TYPE>
static void socketopt_double(OPT_TYPE* i) {
  OPT_TYPE value = *i;
  *i = value * 2;
}

template <typename OPT_TYPE>
static void socketopt_half(OPT_TYPE* i) {
  OPT_TYPE value = *i;
  *i = value / 2;
}

template <typename OPT_TYPE>
static void socketopt_test(int level, int optname) {
  int ret = mufd_socket(AF_INET, SOCK_DGRAM, 0);
  EXPECT_GE(ret, 0);
  if (ret < 0) return;
  int fd = ret;

  /* get */
  OPT_TYPE bufsize;
  socklen_t val_size = sizeof(bufsize);
  ret = mufd_getsockopt(fd, level, optname, &bufsize, &val_size);
  EXPECT_GE(ret, 0);

  /* double */
  socketopt_double<OPT_TYPE>(&bufsize);
  ret = mufd_setsockopt(fd, level, optname, (const void*)&bufsize, val_size);
  EXPECT_GE(ret, 0);
  /* read again */
  OPT_TYPE bufsize_read;
  ret = mufd_getsockopt(fd, level, optname, &bufsize_read, &val_size);
  EXPECT_GE(ret, 0);
  ret = memcmp(&bufsize, &bufsize_read, val_size);
  EXPECT_EQ(ret, 0);

  /* revert back */
  socketopt_half<OPT_TYPE>(&bufsize);
  ret = mufd_setsockopt(fd, level, optname, (const void*)&bufsize, val_size);
  EXPECT_GE(ret, 0);
  /* read again */
  ret = mufd_getsockopt(fd, level, optname, &bufsize_read, &val_size);
  EXPECT_GE(ret, 0);
  ret = memcmp(&bufsize, &bufsize_read, val_size);
  EXPECT_EQ(ret, 0);

  /* expect fail */
  val_size *= 2;
  ret = mufd_getsockopt(fd, level, optname, &bufsize, &val_size);
  EXPECT_LT(ret, 0);
  ret = mufd_setsockopt(fd, level, optname, (const void*)&bufsize, val_size);
  EXPECT_LT(ret, 0);

  ret = mufd_close(fd);
  EXPECT_GE(ret, 0);
}

TEST(Api, socket_snd_buf) { socketopt_test<uint32_t>(SOL_SOCKET, SO_SNDBUF); }
TEST(Api, socket_rcv_buf) { socketopt_test<uint32_t>(SOL_SOCKET, SO_RCVBUF); }

template <>
void socketopt_double(struct timeval* i) {
  i->tv_sec *= 2;
  i->tv_usec *= 2;
}
template <>
void socketopt_half(struct timeval* i) {
  i->tv_sec /= 2;
  i->tv_usec /= 2;
}
TEST(Api, socket_rcvtimeo) { socketopt_test<struct timeval>(SOL_SOCKET, SO_RCVTIMEO); }

GTEST_API_ int main(int argc, char** argv) {
  struct utest_ctx* ctx;
  int ret;
  bool link_flap_wa = true;

  testing::InitGoogleTest(&argc, argv);

  ctx = (struct utest_ctx*)st_test_zmalloc(sizeof(*ctx));
  if (!ctx) {
    err("%s, ctx alloc fail\n", __func__);
    return -ENOMEM;
  }

  g_utest_ctx = ctx;
  utest_ctx_init(ctx);
  utest_parse_args(ctx, argc, argv);
  utest_random_ip(ctx);

  if (ctx->init_params.mt_params.num_ports != 2) {
    err("%s, error, pls pass 2 ports, ex: ./build/tests/KahawaiUfdTest --p_port "
        "0000:af:01.0 --r_port 0000:af:01.1\n",
        __func__);
    utest_ctx_uinit(ctx);
    return -EIO;
  }

  mufd_commit_init_params(&ctx->init_params);

  uint64_t start_time_ns = st_test_get_monotonic_time();

  ret = RUN_ALL_TESTS();

  uint64_t end_time_ns = st_test_get_monotonic_time();
  int time_s = (end_time_ns - start_time_ns) / NS_PER_S;
  int time_least = 10;
  if (link_flap_wa && (time_s < time_least)) {
    /* wa for linkFlapErrDisabled in the hub */
    info("%s, sleep %ds before disable the port\n", __func__, time_least - time_s);
    sleep(time_least - time_s);
  }

  utest_ctx_uinit(ctx);
  return ret;
}