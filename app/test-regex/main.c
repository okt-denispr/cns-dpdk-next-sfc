/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2020 Mellanox Technologies, Ltd
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>

#include <rte_eal.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>
#include <rte_regexdev.h>

#define MAX_FILE_NAME 255
#define MBUF_CACHE_SIZE 256
#define MBUF_SIZE (1 << 8)
#define START_BURST_SIZE 32u

enum app_args {
	ARG_HELP,
	ARG_RULES_FILE_NAME,
	ARG_DATA_FILE_NAME,
	ARG_NUM_OF_JOBS,
	ARG_PERF_MODE,
	ARG_NUM_OF_ITERATIONS,
	ARG_NUM_OF_QPS,
};

struct job_ctx {
	struct rte_mbuf *mbuf;
};

struct qp_params {
	uint32_t total_enqueue;
	uint32_t total_dequeue;
	uint32_t total_matches;
	struct rte_regex_ops **ops;
	struct job_ctx *jobs_ctx;
	char *buf;
};

static void
usage(const char *prog_name)
{
	printf("%s [EAL options] --\n"
		" --rules NAME: precompiled rules file\n"
		" --data NAME: data file to use\n"
		" --nb_jobs: number of jobs to use\n"
		" --perf N: only outputs the performance data\n"
		" --nb_iter N: number of iteration to run\n"
		" --nb_qps N: number of queues to use\n",
		prog_name);
}

static void
args_parse(int argc, char **argv, char *rules_file, char *data_file,
	   uint32_t *nb_jobs, bool *perf_mode, uint32_t *nb_iterations,
	   uint32_t *nb_qps)
{
	char **argvopt;
	int opt;
	int opt_idx;
	size_t len;
	static struct option lgopts[] = {
		{ "help",  0, 0, ARG_HELP},
		/* Rules database file to load. */
		{ "rules",  1, 0, ARG_RULES_FILE_NAME},
		/* Data file to load. */
		{ "data",  1, 0, ARG_DATA_FILE_NAME},
		/* Number of jobs to create. */
		{ "nb_jobs",  1, 0, ARG_NUM_OF_JOBS},
		/* Perf test only */
		{ "perf", 0, 0, ARG_PERF_MODE},
		/* Number of iterations to run with perf test */
		{ "nb_iter", 1, 0, ARG_NUM_OF_ITERATIONS},
		/* Number of QPs. */
		{ "nb_qps", 1, 0, ARG_NUM_OF_QPS},
		/* End of options */
		{ 0, 0, 0, 0 }
	};

	argvopt = argv;
	while ((opt = getopt_long(argc, argvopt, "",
				lgopts, &opt_idx)) != EOF) {
		switch (opt) {
		case ARG_RULES_FILE_NAME:
			len = strnlen(optarg, MAX_FILE_NAME - 1);
			if (len == MAX_FILE_NAME)
				rte_exit(EXIT_FAILURE,
					 "Rule file name to long max %d\n",
					 MAX_FILE_NAME - 1);
			strncpy(rules_file, optarg, MAX_FILE_NAME - 1);
			break;
		case ARG_DATA_FILE_NAME:
			len = strnlen(optarg, MAX_FILE_NAME - 1);
			if (len == MAX_FILE_NAME)
				rte_exit(EXIT_FAILURE,
					 "Data file name to long max %d\n",
					 MAX_FILE_NAME - 1);
			strncpy(data_file, optarg, MAX_FILE_NAME - 1);
			break;
		case ARG_NUM_OF_JOBS:
			*nb_jobs = atoi(optarg);
			break;
		case ARG_PERF_MODE:
			*perf_mode = true;
			break;
		case ARG_NUM_OF_ITERATIONS:
			*nb_iterations = atoi(optarg);
			break;
		case ARG_NUM_OF_QPS:
			*nb_qps = atoi(optarg);
			break;
		case ARG_HELP:
			usage("RegEx test app");
			break;
		default:
			fprintf(stderr, "Invalid option: %s\n", argv[optind]);
			usage("RegEx test app");
			rte_exit(EXIT_FAILURE, "Invalid option\n");
			break;
		}
	}

	if (!perf_mode)
		*nb_iterations = 1;
}

static long
read_file(char *file, char **buf)
{
	FILE *fp;
	long buf_len = 0;
	size_t read_len;
	int res = 0;

	fp = fopen(file, "r");
	if (!fp)
		return -EIO;
	if (fseek(fp, 0L, SEEK_END) == 0) {
		buf_len = ftell(fp);
		if (buf_len == -1) {
			res = EIO;
			goto error;
		}
		*buf = rte_malloc(NULL, sizeof(char) * (buf_len + 1), 4096);
		if (!*buf) {
			res = ENOMEM;
			goto error;
		}
		if (fseek(fp, 0L, SEEK_SET) != 0) {
			res = EIO;
			goto error;
		}
		read_len = fread(*buf, sizeof(char), buf_len, fp);
		if (read_len != (unsigned long)buf_len) {
			res = EIO;
			goto error;
		}
	}
	fclose(fp);
	return buf_len;
error:
	printf("Error, can't open file %s\n, err = %d", file, res);
	if (fp)
		fclose(fp);
	if (*buf)
		rte_free(*buf);
	return -res;
}

static int
clone_buf(char *data_buf, char **buf, long data_len)
{
	char *dest_buf;
	dest_buf =
		rte_malloc(NULL, sizeof(char) * (data_len + 1), 4096);
	if (!dest_buf)
		return -ENOMEM;
	memcpy(dest_buf, data_buf, data_len + 1);
	*buf = dest_buf;
	return 0;
}

static int
init_port(uint16_t *nb_max_payload, char *rules_file, uint8_t *nb_max_matches,
	  uint32_t nb_qps)
{
	uint16_t id;
	uint16_t qp_id;
	uint16_t num_devs;
	char *rules = NULL;
	long rules_len;
	struct rte_regexdev_info info;
	struct rte_regexdev_config dev_conf = {
		.nb_queue_pairs = nb_qps,
		.nb_groups = 1,
	};
	struct rte_regexdev_qp_conf qp_conf = {
		.nb_desc = 1024,
		.qp_conf_flags = 0,
	};
	int res = 0;

	num_devs = rte_regexdev_count();
	if (num_devs == 0) {
		printf("Error, no devices detected.\n");
		return -EINVAL;
	}

	rules_len = read_file(rules_file, &rules);
	if (rules_len < 0) {
		printf("Error, can't read rules files.\n");
		res = -EIO;
		goto error;
	}

	for (id = 0; id < num_devs; id++) {
		res = rte_regexdev_info_get(id, &info);
		if (res != 0) {
			printf("Error, can't get device info.\n");
			goto error;
		}
		printf(":: initializing dev: %d\n", id);
		*nb_max_matches = info.max_matches;
		*nb_max_payload = info.max_payload_size;
		if (info.regexdev_capa & RTE_REGEXDEV_SUPP_MATCH_AS_END_F)
			dev_conf.dev_cfg_flags |=
			RTE_REGEXDEV_CFG_MATCH_AS_END_F;
		dev_conf.nb_max_matches = info.max_matches;
		dev_conf.nb_rules_per_group = info.max_rules_per_group;
		dev_conf.rule_db_len = rules_len;
		dev_conf.rule_db = rules;
		res = rte_regexdev_configure(id, &dev_conf);
		if (res < 0) {
			printf("Error, can't configure device %d.\n", id);
			goto error;
		}
		if (info.regexdev_capa & RTE_REGEXDEV_CAPA_QUEUE_PAIR_OOS_F)
			qp_conf.qp_conf_flags |=
			RTE_REGEX_QUEUE_PAIR_CFG_OOS_F;
		for (qp_id = 0; qp_id < nb_qps; qp_id++) {
			res = rte_regexdev_queue_pair_setup(id, qp_id,
							    &qp_conf);
			if (res < 0) {
				printf("Error, can't setup queue pair %u for "
				       "device %d.\n", qp_id, id);
				goto error;
			}
		}
		printf(":: initializing device: %d done\n", id);
	}
	rte_free(rules);
	return 0;
error:
	if (rules)
		rte_free(rules);
	return res;
}

static void
extbuf_free_cb(void *addr __rte_unused, void *fcb_opaque __rte_unused)
{
}

static int
run_regex(uint32_t nb_jobs,
	  bool perf_mode, uint32_t nb_iterations,
	  uint8_t nb_max_matches, uint32_t nb_qps,
	  char *data_buf, long data_len, long job_len)
{
	char *buf = NULL;
	uint32_t actual_jobs = 0;
	uint32_t i;
	uint16_t qp_id;
	uint16_t dev_id = 0;
	uint8_t nb_matches;
	struct rte_regexdev_match *match;
	long pos;
	unsigned long d_ind = 0;
	struct rte_mbuf_ext_shared_info shinfo;
	int res = 0;
	time_t start;
	time_t end;
	double time;
	struct rte_mempool *mbuf_mp;
	struct qp_params *qp;
	struct qp_params *qps = NULL;
	bool update;
	uint16_t qps_used = 0;

	shinfo.free_cb = extbuf_free_cb;
	mbuf_mp = rte_pktmbuf_pool_create("mbuf_pool", nb_jobs * nb_qps, 0,
			0, MBUF_SIZE, rte_socket_id());
	if (mbuf_mp == NULL) {
		printf("Error, can't create memory pool\n");
		return -ENOMEM;
	}

	qps = rte_malloc(NULL, sizeof(*qps) * nb_qps, 0);
	if (!qps) {
		printf("Error, can't allocate memory for QPs\n");
		res = -ENOMEM;
		goto end;
	}

	for (qp_id = 0; qp_id < nb_qps; qp_id++) {
		struct rte_regex_ops **ops;
		struct job_ctx *jobs_ctx;

		qps_used++;
		qp = &qps[qp_id];
		qp->jobs_ctx = NULL;
		qp->buf = NULL;
		qp->ops = ops = rte_malloc(NULL, sizeof(*ops) * nb_jobs, 0);
		if (!ops) {
			printf("Error, can't allocate memory for ops.\n");
			res = -ENOMEM;
			goto end;
		}

		qp->jobs_ctx = jobs_ctx =
			rte_malloc(NULL, sizeof(*jobs_ctx) * nb_jobs, 0);
		if (!jobs_ctx) {
			printf("Error, can't allocate memory for jobs_ctx.\n");
			res = -ENOMEM;
			goto end;
		}

		/* Allocate the jobs and assign each job with an mbuf. */
		for (i = 0; i < nb_jobs; i++) {
			ops[i] = rte_malloc(NULL, sizeof(*ops[0]) +
					nb_max_matches *
					sizeof(struct rte_regexdev_match), 0);
			if (!ops[i]) {
				printf("Error, can't allocate "
				       "memory for op.\n");
				res = -ENOMEM;
				goto end;
			}
			ops[i]->mbuf = rte_pktmbuf_alloc(mbuf_mp);
			if (!ops[i]->mbuf) {
				printf("Error, can't attach mbuf.\n");
				res = -ENOMEM;
				goto end;
			}
		}

		if (clone_buf(data_buf, &buf, data_len)) {
			printf("Error, can't clone buf.\n");
			res = -EXIT_FAILURE;
			goto end;
		}

		/* Assign each mbuf with the data to handle. */
		actual_jobs = 0;
		pos = 0;
		for (i = 0; (pos < data_len) && (i < nb_jobs) ; i++) {
			long act_job_len = RTE_MIN(job_len, data_len - pos);
			rte_pktmbuf_attach_extbuf(ops[i]->mbuf, &buf[pos], 0,
					act_job_len, &shinfo);
			jobs_ctx[i].mbuf = ops[i]->mbuf;
			ops[i]->mbuf->data_len = job_len;
			ops[i]->mbuf->pkt_len = act_job_len;
			ops[i]->user_id = i;
			ops[i]->group_id0 = 1;
			pos += act_job_len;
			actual_jobs++;
		}

		qp->buf = buf;
		qp->total_matches = 0;
	}

	start = clock();
	for (i = 0; i < nb_iterations; i++) {
		for (qp_id = 0; qp_id < nb_qps; qp_id++) {
			qp = &qps[qp_id];
			qp->total_enqueue = 0;
			qp->total_dequeue = 0;
		}
		do {
			update = false;
			for (qp_id = 0; qp_id < nb_qps; qp_id++) {
				qp = &qps[qp_id];
				if (qp->total_dequeue < actual_jobs) {
					struct rte_regex_ops **
						cur_ops_to_enqueue = qp->ops +
						qp->total_enqueue;

					if (actual_jobs - qp->total_enqueue)
						qp->total_enqueue +=
						rte_regexdev_enqueue_burst
							(dev_id,
							qp_id,
							cur_ops_to_enqueue,
							actual_jobs -
							qp->total_enqueue);
				}
			}
			for (qp_id = 0; qp_id < nb_qps; qp_id++) {
				qp = &qps[qp_id];
				if (qp->total_dequeue < actual_jobs) {
					struct rte_regex_ops **
						cur_ops_to_dequeue = qp->ops +
						qp->total_dequeue;

					qp->total_dequeue +=
						rte_regexdev_dequeue_burst
							(dev_id,
							qp_id,
							cur_ops_to_dequeue,
							qp->total_enqueue -
							qp->total_dequeue);
					update = true;
				}
			}
		} while (update);
	}
	end = clock();
	time = ((double)end - start) / CLOCKS_PER_SEC;
	printf("Job len = %ld Bytes\n",  job_len);
	printf("Time = %lf sec\n",  time);
	printf("Perf = %lf Gbps\n",
	       (((double)actual_jobs * job_len * nb_iterations * 8) / time) /
		1000000000.0);

	if (perf_mode)
		goto end;
	for (qp_id = 0; qp_id < nb_qps; qp_id++) {
		printf("\n############ QP id=%u ############\n", qp_id);
		qp = &qps[qp_id];
		/* Log results per job. */
		for (d_ind = 0; d_ind < qp->total_dequeue; d_ind++) {
			nb_matches = qp->ops[d_ind % actual_jobs]->nb_matches;
			printf("Job id %"PRIu64" number of matches = %d\n",
					qp->ops[d_ind]->user_id, nb_matches);
			qp->total_matches += nb_matches;
			match = qp->ops[d_ind % actual_jobs]->matches;
			for (i = 0; i < nb_matches; i++) {
				printf("match %d, rule = %d, "
				       "start = %d,len = %d\n",
				       i, match->rule_id, match->start_offset,
				       match->len);
				match++;
			}
		}
		printf("Total matches = %d\n", qp->total_matches);
		printf("All Matches:\n");
		/* Log absolute results. */
		for (d_ind = 0; d_ind < qp->total_dequeue; d_ind++) {
			nb_matches = qp->ops[d_ind % actual_jobs]->nb_matches;
			qp->total_matches += nb_matches;
			match = qp->ops[d_ind % actual_jobs]->matches;
			for (i = 0; i < nb_matches; i++) {
				printf("start = %ld, len = %d, rule = %d\n",
						match->start_offset +
						d_ind * job_len,
						match->len, match->rule_id);
				match++;
			}
		}
	}
end:
	for (qp_id = 0; qp_id < qps_used; qp_id++) {
		qp = &qps[qp_id];
		for (i = 0; i < actual_jobs && qp->ops; i++)
			rte_free(qp->ops[i]);
		rte_free(qp->ops);
		qp->ops = NULL;
		for (i = 0; i < actual_jobs && qp->jobs_ctx; i++)
			rte_pktmbuf_free(qp->jobs_ctx[i].mbuf);
		rte_free(qp->jobs_ctx);
		qp->jobs_ctx = NULL;
		rte_free(qp->buf);
		qp->buf = NULL;
	}
	if (mbuf_mp)
		rte_mempool_free(mbuf_mp);
	rte_free(qps);
	return res;
}

int
main(int argc, char **argv)
{
	char rules_file[MAX_FILE_NAME];
	char data_file[MAX_FILE_NAME];
	uint32_t nb_jobs = 0;
	bool perf_mode = 0;
	uint32_t nb_iterations = 0;
	int ret;
	uint16_t nb_max_payload = 0;
	uint8_t nb_max_matches = 0;
	uint32_t nb_qps = 1;
	char *data_buf;
	long data_len;
	long job_len;

	/* Init EAL. */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "EAL init failed\n");
	argc -= ret;
	argv += ret;
	if (argc > 1)
		args_parse(argc, argv, rules_file, data_file, &nb_jobs,
				&perf_mode, &nb_iterations, &nb_qps);

	if (nb_qps == 0)
		rte_exit(EXIT_FAILURE, "Number of QPs must be greater than 0\n");
	ret = init_port(&nb_max_payload, rules_file,
			&nb_max_matches, nb_qps);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "init port failed\n");

	data_len = read_file(data_file, &data_buf);
	if (data_len <= 0)
		rte_exit(EXIT_FAILURE, "Error, can't read file, or file is empty.\n");

	job_len = data_len / nb_jobs;
	if (job_len == 0)
		rte_exit(EXIT_FAILURE, "Error, To many jobs, for the given input.\n");

	if (job_len > nb_max_payload)
		rte_exit(EXIT_FAILURE, "Error, not enough jobs to cover input.\n");

	ret = run_regex(nb_jobs, perf_mode,
			nb_iterations, nb_max_matches, nb_qps,
			data_buf, data_len, job_len);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "RegEx function failed\n");
	}
	rte_free(data_buf);
	return EXIT_SUCCESS;
}
