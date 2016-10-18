/*
 **************************************************************************
 * Copyright (c) 2014, 2016-2017, The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

#include "nss_qdisc.h"

/*
 * nss_blackhole private qdisc structure
 */
struct nss_blackhole_sched_data {
	struct nss_qdisc nq;	/* Common base class for all nss qdiscs */
	u8 set_default;		/* Flag to set qdisc as default qdisc for enqueue */
};

/*
 * nss_blackhole_enqueue()
 *	Enqueue API for nss blackhole qdisc.
 */
static int nss_blackhole_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	return nss_qdisc_enqueue(skb, sch);
}

/*
 * nss_blackhole_dequeue()
 * 	Dequeue API for nss blackhole qdisc.
 */
static struct sk_buff *nss_blackhole_dequeue(struct Qdisc *sch)
{
	return nss_qdisc_dequeue(sch);
}

/*
 * nss_blackhole_drop()
 *	The following function drops a packet from HLOS queue.
 *
 * Note, this does not drop packets from queues in the NSS. We do not support that.
 */
static unsigned int nss_blackhole_drop(struct Qdisc *sch)
{
	nss_qdisc_info("%s: qdisc %x dropping\n", __func__, sch->handle);
	return nss_qdisc_drop(sch);
}

/*
 * nss_blackhole_reset()
 *	Resets the nss blackhole qdisc.
 */
static void nss_blackhole_reset(struct Qdisc *sch)
{
	nss_qdisc_info("%s: qdisc %x resetting\n", __func__, sch->handle);
	nss_qdisc_reset(sch);
}

/*
 * nss_blackhole_destroy()
 *	Destroys the nss blackhole qdisc.
 */
static void nss_blackhole_destroy(struct Qdisc *sch)
{
	struct nss_qdisc *nq = (struct nss_qdisc *)qdisc_priv(sch);

	/*
	 * Stop the polling of basic stats
	 */
	nss_qdisc_stop_basic_stats_polling(nq);

	nss_qdisc_info("%s: destroying qdisc %x\n", __func__, sch->handle);
	nss_qdisc_destroy(nq);
}

/*
 * nss_blackhole policy structure
 */
static const struct nla_policy nss_blackhole_policy[TCA_NSSBLACKHOLE_MAX + 1] = {
	[TCA_NSSBLACKHOLE_PARMS] = { .len = sizeof(struct tc_nssblackhole_qopt) },
};

/*
 * nss_blackhole_change()
 *	Function call used to configure the parameters of the nss blackhole qdisc.
 */
static int nss_blackhole_change(struct Qdisc *sch, struct nlattr *opt)
{
	struct nss_blackhole_sched_data *q;
	struct nlattr *na[TCA_NSSBLACKHOLE_MAX + 1];
	struct tc_nssblackhole_qopt *qopt;
	int err;
	struct nss_if_msg nim;

	q = qdisc_priv(sch);

	if (opt == NULL) {
		return -EINVAL;
	}

	err = nla_parse_nested(na, TCA_NSSBLACKHOLE_MAX, opt, nss_blackhole_policy);
	if (err < 0)
		return err;

	if (na[TCA_NSSBLACKHOLE_PARMS] == NULL)
		return -EINVAL;

	qopt = nla_data(na[TCA_NSSBLACKHOLE_PARMS]);

	/*
	 * Required for basic stats display
	 */
	sch->limit = 0;

	q->set_default = qopt->set_default;
	nss_qdisc_info("%s: qdisc set_default = %u\n", __func__, qopt->set_default);

	/*
	 * Underneath nss_bloackhole uses a fifo in the NSS. This is why we are sending down a configuration
	 * message to a fifo node. There are no blackhole shaper in the NSS as yet.
	 *
	 * Note: We simply set the limit of fifo to zero to get the blackhole behavior.
	 */
	nim.msg.shaper_configure.config.msg.shaper_node_config.qos_tag = q->nq.qos_tag;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.fifo_param.limit = 0;
	nim.msg.shaper_configure.config.msg.shaper_node_config.snc.fifo_param.drop_mode = NSS_SHAPER_FIFO_DROP_MODE_TAIL;
	if (nss_qdisc_configure(&q->nq, &nim, NSS_SHAPER_CONFIG_TYPE_FIFO_CHANGE_PARAM) < 0) {
		nss_qdisc_error("%s: qdisc %x configuration failed\n", __func__, sch->handle);
		return -EINVAL;
	}

	/*
	 * There is nothing we need to do if the qdisc is not
	 * set as default qdisc.
	 */
	if (q->set_default == 0)
		return 0;

	/*
	 * Set this qdisc to be the default qdisc for enqueuing packets.
	 */
	if (nss_qdisc_set_default(&q->nq) < 0) {
		nss_qdisc_error("%s: qdisc %x set_default failed\n", __func__, sch->handle);
		return -EINVAL;
	}

	nss_qdisc_info("%s: qdisc %x set as default\n", __func__, q->nq.qos_tag);
	return 0;
}

/*
 * nss_blackhole_init()
 *	Initializes a nss blackhole qdisc.
 */
static int nss_blackhole_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct nss_qdisc *nq = qdisc_priv(sch);

	if (opt == NULL)
		return -EINVAL;

	nss_qdisc_info("%s: qdisc %x initializing\n", __func__, sch->handle);
	nss_blackhole_reset(sch);

	if (nss_qdisc_init(sch, nq, NSS_QDISC_MODE_NSS, NSS_SHAPER_NODE_TYPE_FIFO, 0) < 0)
		return -EINVAL;

	nss_qdisc_info("%s: qdisc %x initialized with parent %x\n", __func__, sch->handle, sch->parent);
	if (nss_blackhole_change(sch, opt) < 0) {
		nss_qdisc_destroy(nq);
		return -EINVAL;
	}

	/*
	 * Start the stats polling timer
	 */
	nss_qdisc_start_basic_stats_polling(nq);

	return 0;
}

/*
 * nss_blackhole_dump()
 *	Dumps qdisc parameters for nss blackhole.
 */
static int nss_blackhole_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct nss_blackhole_sched_data *q;
	struct nlattr *opts = NULL;
	struct tc_nssblackhole_qopt opt;

	nss_qdisc_info("%s: qdisc %x dumping!\n", __func__, sch->handle);

	q = qdisc_priv(sch);
	if (q == NULL) {
		return -1;
	}

	opt.set_default = q->set_default;

	opts = nla_nest_start(skb, TCA_OPTIONS);
	if (opts == NULL) {
		goto nla_put_failure;
	}
	if (nla_put(skb, TCA_NSSBLACKHOLE_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	return nla_nest_end(skb, opts);

nla_put_failure:
	nla_nest_cancel(skb, opts);
	return -EMSGSIZE;
}

/*
 * nss_blackhole_peek()
 *	Peeks the first packet in queue for this qdisc.
 *
 * Note: This just peeks at the first packet in what is present in HLOS. This does not
 * perform an actual peak into the queue in the NSS. Given the async delay between
 * the processors, there is less use in implementing this function.
 */
static struct sk_buff *nss_blackhole_peek(struct Qdisc *sch)
{
	nss_qdisc_info("%s: qdisc %x peeked\n", __func__, sch->handle);
	return nss_qdisc_peek(sch);
}

/*
 * Registration structure for nss blackhole qdisc
 */
struct Qdisc_ops nss_blackhole_qdisc_ops __read_mostly = {
	.id		=	"nssblackhole",
	.priv_size	=	sizeof(struct nss_blackhole_sched_data),
	.enqueue	=	nss_blackhole_enqueue,
	.dequeue	=	nss_blackhole_dequeue,
	.peek		=	nss_blackhole_peek,
	.drop		=	nss_blackhole_drop,
	.init		=	nss_blackhole_init,
	.reset		=	nss_blackhole_reset,
	.destroy	=	nss_blackhole_destroy,
	.change		=	nss_blackhole_change,
	.dump		=	nss_blackhole_dump,
	.owner		=	THIS_MODULE,
};
