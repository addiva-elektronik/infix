/* SPDX-License-Identifier: BSD-3-Clause */

#include <srx/common.h>
#include <srx/lyx.h>
#include <srx/srx_val.h>

#include "core.h"

#define XPATH_BASE_ "/ietf-routing:routing/control-plane-protocols/control-plane-protocol"
#define XPATH_OSPF_ XPATH_BASE_ "/ietf-ospf:ospf"
#define STATICD_CONF "/etc/frr/static.d/confd.conf"
#define STATICD_CONF_NEXT STATICD_CONF "+"
#define STATICD_CONF_PREV STATICD_CONF "-"
#define OSPFD_CONF "/etc/frr/ospfd.conf"
#define OSPFD_CONF_NEXT OSPFD_CONF "+"
#define OSPFD_CONF_PREV OSPFD_CONF "-"
#define BFDD_CONF "/etc/frr/bfd_enabled" /* Just signal that bfd should be enabled*/
#define BFDD_CONF_NEXT BFDD_CONF "+"

#define FRR_STATIC_CONFIG "! Generated by Infix confd\n\
frr defaults traditional\n\
hostname Router\n\
password zebra \n\
enable password zebra\n\
no log unique-id\n\
log syslog informational\n\
log facility local2\n"

int parse_ospf_interfaces(sr_session_ctx_t *session, struct lyd_node *areas, FILE *fp)
{
	struct lyd_node *interface, *interfaces, *area;
	int num_bfd_enabled = 0;

	LY_LIST_FOR(lyd_child(areas), area) {
		const char *area_id;

		interfaces = lydx_get_child(area, "interfaces");
		area_id = lydx_get_cattr(area, "area-id");

		LY_LIST_FOR(lyd_child(interfaces), interface) {
			const char *hello, *dead, *retransmit, *transmit, *interface_type, *cost;

			if (lydx_get_bool(interface, "enabled")) {
				int passive = 0, bfd_enabled = 0;
				struct lyd_node *bfd;

				bfd = lydx_get_child(interface, "bfd");
				bfd_enabled = lydx_get_bool(bfd, "enabled");
				num_bfd_enabled += bfd_enabled;

				passive = lydx_get_bool(interface, "passive");
				fprintf(fp, "interface %s\n", lydx_get_cattr(interface, "name"));

				hello = lydx_get_cattr(interface, "hello-interval");
				dead = lydx_get_cattr(interface, "dead-interval");
				retransmit = lydx_get_cattr(interface, "retransmit-interval");
				transmit = lydx_get_cattr(interface, "transmit-delay");
				interface_type = lydx_get_cattr(interface, "interface-type");
				cost = lydx_get_cattr(interface, "cost");

				fprintf(fp, "  ip ospf area %s\n", area_id);
				if (dead)
					fprintf(fp, "  ip ospf dead-interval %s\n", dead);
				if (hello)
					fprintf(fp, "  ip ospf hello-interval %s\n", hello);
				if (retransmit)
					fprintf(fp, "  ip ospf retransmit-interval %s\n", retransmit);
				if (transmit)
					fprintf(fp, "  ip ospf transmit-delay %s\n", transmit);
				if (bfd_enabled)
					fputs("  ip ospf bfd\n", fp);
				if (passive)
					fputs("  ip ospf passive\n", fp);
				if (interface_type)
					fprintf(fp, "  ip ospf network %s\n", interface_type);
				if (cost)
					fprintf(fp, "  ip ospf cost %s\n", cost);
			}
		}
	}

	return num_bfd_enabled;
}

int parse_ospf_redistribute(sr_session_ctx_t *session, struct lyd_node *redistributes, FILE *fp)
{
	struct lyd_node *tmp;

	LY_LIST_FOR(lyd_child(redistributes), tmp) {
		const char *protocol = lydx_get_cattr(tmp, "protocol");

		fprintf(fp, "  redistribute %s\n", protocol);
	}

	return 0;
}

int parse_ospf_areas(sr_session_ctx_t *session, struct lyd_node *areas, FILE *fp)
{
	int areas_configured = 0;
	struct lyd_node *area;

	LY_LIST_FOR(lyd_child(areas), area) {
		const char *area_id, *area_type, *default_cost;
		int summary;

		area_id = lydx_get_cattr(area, "area-id");
		area_type = lydx_get_cattr(area, "area-type");
		default_cost = lydx_get_cattr(area, "default-cost");
		summary = lydx_get_bool(area, "summary");

		if (area_type) {
			int stub_or_nssa = 0;

			if (!strcmp(area_type, "ietf-ospf:nssa-area")) {
				stub_or_nssa = 1;
				fprintf(fp, "  area %s nssa %s\n", area_id, !summary ? "no-summary" : "");
			} else if (!strcmp(area_type, "ietf-ospf:stub-area")) {
				stub_or_nssa = 1;
				fprintf(fp, "  area %s stub %s\n", area_id, !summary ? "no-summary" : "");
			}
			if (stub_or_nssa && default_cost)
				fprintf(fp, "  area %s default-cost %s\n", area_id, default_cost);
		}
		areas_configured++;
	}

	return areas_configured;
}

int parse_ospf(sr_session_ctx_t *session, struct lyd_node *ospf)
{
	const char *static_debug = "! OSPF default debug\
debug ospf bfd\n\
debug ospf packet all detail\n\
debug ospf ism\n\
debug ospf nsm\n\
debug ospf default-information\n\
debug ospf nssa\n\
! OSPF configuration\n";
	struct lyd_node *areas, *default_route;
	const char *router_id;
	int bfd_enabled = 0;
	int num_areas = 0;
	FILE *fp;

	fp = fopen(OSPFD_CONF_NEXT, "w");
	if (!fp) {
		ERROR("Failed to open %s", OSPFD_CONF_NEXT);
		return SR_ERR_INTERNAL;
	}

	fputs(FRR_STATIC_CONFIG, fp);
	fputs(static_debug, fp);

	areas = lydx_get_child(ospf, "areas");
	router_id = lydx_get_cattr(ospf, "explicit-router-id");
	bfd_enabled = parse_ospf_interfaces(session, areas, fp);
	fputs("router ospf\n", fp);
	num_areas = parse_ospf_areas(session, areas, fp);
	parse_ospf_redistribute(session, lydx_get_child(ospf, "redistribute"), fp);
	default_route = lydx_get_child(ospf, "default-route-advertise");
	if (default_route) {
		/* enable is obsolete in favor for enabled. */
		if ((lydx_get_child(default_route, "enable") && lydx_get_bool(default_route, "enable"))
		    || lydx_get_bool(default_route, "enabled")) {
			fputs("  default-information originate", fp);
			if (lydx_get_bool(default_route, "always"))
				fputs(" always", fp);
			fputs("\n", fp);
		}
	}

	if (router_id)
		fprintf(fp, "  ospf router-id %s\n", router_id);
	fclose(fp);

	if (!bfd_enabled)
		(void)remove(BFDD_CONF);

	if (!num_areas) {
		(void)remove(OSPFD_CONF_NEXT);
		return 0;
	}

	if (bfd_enabled)
		(void)touch(BFDD_CONF_NEXT);

	return 0;
}

static int parse_route(struct lyd_node *parent, FILE *fp, const char *ip)
{
	const char *outgoing_interface, *next_hop_address, *special_next_hop,
		*destination_prefix, *route_preference;
	struct lyd_node *next_hop;

	destination_prefix = lydx_get_cattr(parent, "destination-prefix");
	route_preference = lydx_get_cattr(parent, "route-preference");
	next_hop = lydx_get_child(parent, "next-hop");
	outgoing_interface = lydx_get_cattr(next_hop, "outgoing-interface");
	next_hop_address = lydx_get_cattr(next_hop, "next-hop-address");
	special_next_hop = lydx_get_cattr(next_hop, "special-next-hop");

	fprintf(fp, "%s route %s ", ip, destination_prefix);

	/* There can only be one */
	if (outgoing_interface)
		fputs(outgoing_interface, fp);
	else if (next_hop_address)
		fputs(next_hop_address, fp);
	else if (strcmp(special_next_hop, "blackhole") == 0)
		fputs("blackhole", fp);
	else if (strcmp(special_next_hop, "unreachable") == 0)
		fputs("reject", fp);
	else if (strcmp(special_next_hop, "receive") == 0)
		fputs("Null0", fp);
	fprintf(fp, " %s\n", route_preference);

	return SR_ERR_OK;
}

static int parse_static_routes(sr_session_ctx_t *session, struct lyd_node *parent, FILE *fp)
{
	struct lyd_node *ipv4, *v4routes, *ipv6, *v6routes, *route;
	int num_routes = 0;

	ipv4 = lydx_get_child(parent, "ipv4");
	ipv6 = lydx_get_child(parent, "ipv6");

	v4routes = lydx_get_child(ipv4, "route");
	LY_LIST_FOR(v4routes, route) {
		parse_route(route, fp, "ip");
		num_routes++;
	}

	v6routes = lydx_get_child(ipv6, "route");
	LY_LIST_FOR(v6routes, route) {
		parse_route(route, fp, "ipv6");
		num_routes++;
	}

	DEBUG("Found %d routes in configuration", num_routes);
	return num_routes;
}

static int change_control_plane_protocols(sr_session_ctx_t *session, uint32_t sub_id, const char *module,
                                          const char *xpath, sr_event_t event, unsigned request_id, void *priv)
{
	int staticd_enabled = 0, ospfd_enabled = 0, bfdd_enabled = 0;
	bool ospfd_running, bfdd_running;
	struct lyd_node *cplane, *tmp;
	bool restart_zebra = false;
	int rc = SR_ERR_OK;
	sr_data_t *cfg;
	FILE *fp;

	switch (event) {
	case SR_EV_ENABLED: /* first time, on register. */
	case SR_EV_CHANGE: /* regular change (copy cand running) */
		fp = fopen(STATICD_CONF_NEXT, "w");
		if (!fp) {
			ERROR("Failed to open %s", STATICD_CONF_NEXT);
			return SR_ERR_INTERNAL;
		}
		fputs("! Generated by Infix confd\n", fp);
		break;

	case SR_EV_ABORT: /* User abort, or other plugin failed */
		(void)remove(STATICD_CONF_NEXT);
		return SR_ERR_OK;

	case SR_EV_DONE:
		/* Check if passed validation in previous event */
		staticd_enabled = fexist(STATICD_CONF_NEXT);
		ospfd_enabled = fexist(OSPFD_CONF_NEXT);
		bfdd_enabled = fexist(BFDD_CONF_NEXT);
		ospfd_running = !systemf("initctl -bfq status ospfd");
		bfdd_running = !systemf("initctl -bfq status bfdd");

		if (bfdd_running && !bfdd_enabled) {
			if (systemf("initctl -bfq disable bfdd")) {
				ERROR("Failed to disable BFD routing daemon");
				rc = SR_ERR_INTERNAL;
				goto err_abandon;
			}
			/* Remove all generated files */
			(void)remove(BFDD_CONF);
		}

		if (ospfd_running && !ospfd_enabled) {
			if (systemf("initctl -bfq disable ospfd")) {
				ERROR("Failed to disable OSPF routing daemon");
				rc = SR_ERR_INTERNAL;
				goto err_abandon;
			}
			/* Remove all generated files */
			(void)remove(OSPFD_CONF);
		}

		if (bfdd_enabled) {
			(void)rename(BFDD_CONF_NEXT, BFDD_CONF);
			if (!bfdd_running) {
				if (systemf("initctl -bfq enable bfdd")) {
					ERROR("Failed to enable OSPF routing daemon");
					rc = SR_ERR_INTERNAL;
					goto err_abandon;
				}
			}
		}

		if (ospfd_enabled) {
			(void)remove(OSPFD_CONF_PREV);
			(void)rename(OSPFD_CONF, OSPFD_CONF_PREV);
			(void)rename(OSPFD_CONF_NEXT, OSPFD_CONF);
			if (!ospfd_running) {
				if (systemf("initctl -bnq enable ospfd")) {
					ERROR("Failed to enable OSPF routing daemon");
					rc = SR_ERR_INTERNAL;
					goto err_abandon;
				}
			} else {
				restart_zebra = true;
			}
		}

		if (staticd_enabled) {
			(void)remove(STATICD_CONF_PREV);
			(void)rename(STATICD_CONF, STATICD_CONF_PREV);
			(void)rename(STATICD_CONF_NEXT, STATICD_CONF);
			restart_zebra = true;
		} else {
			if (!remove(STATICD_CONF))
				restart_zebra = true;
		}

		if (restart_zebra) {
			/* skip in runlevel S, no routing daemons run here anyway */
			if (systemf("runlevel >/dev/null 2>&1"))
				return SR_ERR_OK;

			if (systemf("initctl -bfq restart zebra")) {
				ERROR("Failed to restart zebra routing daemon");
				rc = SR_ERR_INTERNAL;
				goto err_abandon;
			}
		}

		return SR_ERR_OK;
	default:
		return SR_ERR_OK;
	}

	rc = sr_get_data(session, "/ietf-routing:routing/control-plane-protocols//.", 0, 0, 0, &cfg);
	if (rc || !cfg) {
		NOTE("No control-plane-protocols available.");
		goto err_close;
	}

	LY_LIST_FOR(lyd_child(cfg->tree), tmp) {
		LY_LIST_FOR(lyd_child(tmp), cplane) {
			const char *type;

			type = lydx_get_cattr(cplane, "type");
			if (!strcmp(type, "infix-routing:static")) {
				staticd_enabled = parse_static_routes(session, lydx_get_child(cplane, "static-routes"), fp);
			} else if (!strcmp(type, "infix-routing:ospfv2")) {
				parse_ospf(session, lydx_get_child(cplane, "ospf"));
			}
		}
	}
	sr_release_data(cfg);

err_close:
	fclose(fp);
	if (!staticd_enabled)
		(void)remove(STATICD_CONF_NEXT);

err_abandon:
	return rc;
}

int ietf_routing_init(struct confd *confd)
{
	int rc = 0;

	REGISTER_CHANGE(confd->session, "ietf-routing", "/ietf-routing:routing/control-plane-protocols",
			0, change_control_plane_protocols, confd, &confd->sub);

	return SR_ERR_OK;
fail:
	ERROR("Init routing failed: %s", sr_strerror(rc));
	return rc;
}
