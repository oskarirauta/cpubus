#include <algorithm>

#include "common.hpp"
#include "cpu.hpp"
#include "shared.hpp"
#include "ubus_cmds.hpp"

const struct blobmsg_policy get_policy[] = {
	[CPU_ID] = { .name = "id", .type = BLOBMSG_TYPE_INT16 },
	[CPU_NAME] = { .name = "name", .type = BLOBMSG_TYPE_STRING },
	[CPU_CMD] = { .name = "cmd", .type = BLOBMSG_TYPE_STRING },
	[CPU_FIELDS] = { .name = "fields", .type = BLOBMSG_TYPE_STRING },
};

int get_policy_size(void) {
	return ARRAY_SIZE(get_policy);
}

int cpubus_get(struct ubus_context *ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *method,
		struct blob_attr *msg) {

	struct blob_attr *tb[__GET_ARGS_MAX];
	blobmsg_parse(get_policy, ARRAY_SIZE(get_policy), tb, blob_data(msg), blob_len(msg));
	int index = 0;
	bool fields[NUM_FIELDS] = { false, false, false, false, false, false, false };
	bool selective = false;
	bool only_count = false;

	std::lock_guard<std::mutex> guard(cpu_mutex);

	if ( tb[CPU_CMD] ) {
		std::string cmd = common::to_lower(std::string((char*)blobmsg_data(tb[CPU_CMD])));
		if ( cmd == "count" ) return cpubus_count(ctx, obj, req, method, msg);
		else if ( cmd == "load" || cmd == "total" ) return cpubus_load(ctx, obj, req, method, msg);
		else if ( cmd == "die" || cmd == "exit" ) return cpubus_exit(ctx, obj, req, method, msg);
		else return UBUS_STATUS_INVALID_ARGUMENT;
	}

	if ( tb[CPU_ID] ) {
		int cpu_id = blobmsg_get_u32(tb[CPU_ID]);
		if ( cpu_id >= 0 && cpu_id < cpu_data -> count) index = cpu_id + 1;
		else index = -1;
	}

	if ( index == 0 && tb[CPU_NAME] ) {
		index = cpu_data -> indexOf(common::to_lower(std::string((char*)blobmsg_data(tb[CPU_NAME]))));
		if ( !index ) index = -1;
	}

	if ( index != 1 && tb[CPU_FIELDS] ) {
		std::string fields_str = common::to_lower(std::string((char*)blobmsg_data(tb[CPU_FIELDS])));
		if ( common::trim(fields_str, ", \t\r\n").empty()) return UBUS_STATUS_INVALID_ARGUMENT;
		std::vector<std::string> fields_v = common::split(fields_str, ',', " \t\r\n");
		std::for_each(std::begin(fields_v), std::end(fields_v), [&fields, &selective, &only_count](std::string& field) {

			switch ( common::hash(field.c_str())) {
				case common::hash("count"):
					if ( !selective && !fields[FIELD_COUNT]) only_count = true;
					fields[FIELD_COUNT] = true;
					selective = true;
					break;
				case common::hash("id"): fields[FIELD_ID] = true; selective = true; only_count = false; break;
				case common::hash("name"): fields[FIELD_NAME] = true; selective = true; only_count = false; break;
				case common::hash("vendor"): fields[FIELD_VENDOR] = true; selective = true; only_count = false; break;
				case common::hash("model"): fields[FIELD_MODEL] = true; selective = true; only_count = false; break;
				case common::hash("mhz"): fields[FIELD_MHZ] = true; selective = true; only_count = false; break;
				case common::hash("load"): fields[FIELD_LOAD] = true; selective = true; only_count = false; break;
			}
		});

		if ( !selective ) return UBUS_STATUS_INVALID_ARGUMENT;
	}

	if ( index == -1 ) return UBUS_STATUS_INVALID_ARGUMENT;
	else if ( index == 0 )
		if ( selective ) {
			if ( !fields[FIELD_COUNT] && !fields[FIELD_LOAD])
				return UBUS_STATUS_INVALID_ARGUMENT;
			if ( fields[FIELD_COUNT]) blobmsg_add_u16(&b, "count", cpu_data -> count);
			if ( fields[FIELD_LOAD]) blobmsg_add_u16(&b, "load", static_cast<int>(cpu_data -> load));
		} else blobmsg_add_u16(&b, "load", static_cast<int>(cpu_data -> load));
	else {
		index--;
		if ( selective ) {
			if ( only_count ) return UBUS_STATUS_INVALID_ARGUMENT;
			if ( fields[FIELD_ID]) blobmsg_add_u16(&b, "id", cpu_data -> nodes[index].id);
			if ( fields[FIELD_NAME]) blobmsg_add_string(&b, "name", cpu_data -> nodes[index].name.c_str());
			if ( fields[FIELD_VENDOR]) blobmsg_add_string(&b, "vendor", cpu_data -> nodes[index].vendor.c_str());
			if ( fields[FIELD_MODEL]) blobmsg_add_string(&b, "model", cpu_data -> nodes[index].model.c_str());
			if ( fields[FIELD_MHZ]) blobmsg_add_u16(&b, "mhz", cpu_data -> nodes[index].mhz);
			if ( fields[FIELD_LOAD]) blobmsg_add_u16(&b, "load", static_cast<int>(cpu_data -> nodes[index].load));
		} else {
			blobmsg_add_u16(&b, "id", cpu_data -> nodes[index].id);
			blobmsg_add_string(&b, "name", cpu_data -> nodes[index].name.c_str());
			blobmsg_add_u16(&b, "load", static_cast<int>(cpu_data -> nodes[index].load));
		}
	}
	ubus_send_reply(ctx, req, b.head);

	return 0;
}

int cpubus_count(struct ubus_context *ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *method,
		struct blob_attr *msg) {

	blob_buf_init(&b, 0);
	std::lock_guard<std::mutex> guard(cpu_mutex);
	blobmsg_add_u16(&b, "count", cpu_data -> count);
	ubus_send_reply(ctx, req, b.head);
	return 0;
}

int cpubus_load(struct ubus_context *ctx, struct ubus_object *obj,
		  struct ubus_request_data *req, const char *method,
		  struct blob_attr *msg) {

	blob_buf_init(&b, 0);
	std::lock_guard<std::mutex> guard(cpu_mutex);
	blobmsg_add_u16(&b, "load", static_cast<int>(cpu_data -> load));
	ubus_send_reply(ctx, req, b.head);
	return 0;
}

int cpubus_list(struct ubus_context *ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *method,
		struct blob_attr *msg) {

	blob_buf_init(&b, 0);
	std::lock_guard<std::mutex> guard(cpu_mutex);
	blobmsg_add_u16(&b, "count", cpu_data -> count);
	blobmsg_add_u16(&b, "load", static_cast<int>(cpu_data -> load));

	std::for_each(std::begin(cpu_data -> nodes), std::end(cpu_data -> nodes), [](cpu_node& node) {
		void *cookie = blobmsg_open_table(&b, node.name.c_str());
		blobmsg_add_u16(&b, "id", node.id);
		blobmsg_add_u16(&b, "load", static_cast<int>(node.load));
		blobmsg_close_table(&b, cookie);
	});

	ubus_send_reply(ctx, req, b.head);
	return 0;
}

int cpubus_all(struct ubus_context *ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *method,
		struct blob_attr *msg) {

	blob_buf_init(&b, 0);
	std::lock_guard<std::mutex> guard(cpu_mutex);
	blobmsg_add_u16(&b, "total", static_cast<int>(cpu_data -> load));

	std::for_each(std::begin(cpu_data -> nodes), std::end(cpu_data -> nodes), [](cpu_node& node) {
		blobmsg_add_u16(&b, node.name.c_str(), static_cast<int>(node.load));
	});

	ubus_send_reply(ctx, req, b.head);
	return 0;
}

int cpubus_exit(struct ubus_context *ctx, struct ubus_object *obj,
		struct ubus_request_data *req, const char *method,
		struct blob_attr *msg) {

	logVerbose << APP_NAME << ": ubus received die request" << std::endl;
	uloop_end();
	
	return 0;
}
