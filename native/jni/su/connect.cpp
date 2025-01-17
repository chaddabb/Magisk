#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

#include <daemon.h>
#include <utils.h>
#include <logging.h>

#include "su.h"

using namespace std;

enum {
	NAMED_ACTIVITY,
	PKG_ACTIVITY,
	CONTENT_PROVIDER
};

#define CALL_PROVIDER \
"/system/bin/app_process", "/system/bin", "com.android.commands.content.Content", \
"call", "--uri", target, "--user", user, "--method", action

#define START_ACTIVITY \
"/system/bin/app_process", "/system/bin", "com.android.commands.am.Am", \
"start", "-p", target, "--user", user, "-a", "android.intent.action.VIEW", \
"-f", "0x18000020", "--es", "action", action

// 0x18000020 = FLAG_ACTIVITY_NEW_TASK|FLAG_ACTIVITY_MULTIPLE_TASK|FLAG_INCLUDE_STOPPED_PACKAGES

#define get_user(info) \
(info->cfg[SU_MULTIUSER_MODE] == MULTIUSER_MODE_USER \
? info->uid / 100000 : 0)

#define get_uid(info) \
(info->cfg[SU_MULTIUSER_MODE] == MULTIUSER_MODE_OWNER_MANAGED \
? info->uid % 100000 : info->uid)

#define get_cmd(to) \
(to.command[0] ? to.command : to.shell[0] ? to.shell : DEFAULT_SHELL)

class Extra {
	const char *key;
	enum {
		INT,
		BOOL,
		STRING
	} type;
	union {
		int int_val;
		bool bool_val;
		const char * str_val;
	};
	char i_buf[16];
	char b_buf[32];
public:
	Extra(const char *k, int v): key(k), type(INT), int_val(v) {}
	Extra(const char *k, bool v): key(k), type(BOOL), bool_val(v) {}
	Extra(const char *k, const char *v): key(k), type(STRING), str_val(v) {}

	void add_intent(vector<const char *> &vec) {
		const char *val;
		switch (type) {
			case INT:
				vec.push_back("--ei");
				sprintf(i_buf, "%d", int_val);
				val = i_buf;
				break;
			case BOOL:
				vec.push_back("--ez");
				val = bool_val ? "true" : "false";
				break;
			case STRING:
				vec.push_back("--es");
				val = str_val;
				break;
		}
		vec.push_back(key);
		vec.push_back(val);
	}

	void add_bind(vector<const char *> &vec) {
		switch (type) {
			case INT:
				sprintf(b_buf, "%s:i:%d", key, int_val);
				break;
			case BOOL:
				sprintf(b_buf, "%s:b:%s", key, bool_val ? "true" : "false");
				break;
			case STRING:
				sprintf(b_buf, "%s:s:%s", key, str_val);
				break;
		}
		vec.push_back("--extra");
		vec.push_back(b_buf);
	}
};

static bool check_error(int fd) {
	char buf[1024];
	unique_ptr<FILE, decltype(&fclose)> out(xfdopen(fd, "r"), fclose);
	while (fgets(buf, sizeof(buf), out.get())) {
		if (strncmp(buf, "Error", 5) == 0)
			return false;
	}
	return true;
}

static void exec_cmd(const char *action, vector<Extra> &data,
					 const shared_ptr<su_info> &info, int mode = CONTENT_PROVIDER) {
	char target[128];
	char user[4];
	sprintf(user, "%d", get_user(info));

	// First try content provider call method
	if (mode >= CONTENT_PROVIDER) {
		sprintf(target, "content://%s.provider", info->str[SU_MANAGER].data());
		vector<const char *> args{ CALL_PROVIDER };
		for (auto &e : data) {
			e.add_bind(args);
		}
		args.push_back(nullptr);
		exec_t exec {
			.err = true,
			.fd = -1,
			.pre_exec = [] { setenv("CLASSPATH", "/system/framework/content.jar", 1); },
			.argv = args.data()
		};
		exec_command_sync(exec);
		if (check_error(exec.fd))
			return;
	}

	vector<const char *> args{ START_ACTIVITY };
	for (auto &e : data) {
		e.add_intent(args);
	}
	args.push_back(nullptr);
	exec_t exec {
		.err = true,
		.fd = -1,
		.pre_exec = [] { setenv("CLASSPATH", "/system/framework/am.jar", 1); },
		.argv = args.data()
	};

	if (mode >= PKG_ACTIVITY) {
		// Then try start activity without component name
		strcpy(target, info->str[SU_MANAGER].data());
		exec_command_sync(exec);
		if (check_error(exec.fd))
			return;
	}

	// Finally, fallback to start activity with component name
	args[4] = "-n";
	sprintf(target, "%s/a.m", info->str[SU_MANAGER].data());
	exec.fd = -2;
	exec.fork = fork_dont_care;
	exec_command(exec);
}

void app_log(const su_context &ctx) {
	if (fork_dont_care() == 0) {
		vector<Extra> extras;
		extras.reserve(6);
		extras.emplace_back("from.uid", get_uid(ctx.info));
		extras.emplace_back("to.uid", ctx.req.uid);
		extras.emplace_back("pid", ctx.pid);
		extras.emplace_back("policy", ctx.info->access.policy);
		extras.emplace_back("command", get_cmd(ctx.req));
		extras.emplace_back("notify", (bool) ctx.info->access.notify);

		exec_cmd("log", extras, ctx.info);
		exit(0);
	}
}

void app_notify(const su_context &ctx) {
	if (fork_dont_care() == 0) {
		vector<Extra> extras;
		extras.reserve(2);
		extras.emplace_back("from.uid", get_uid(ctx.info));
		extras.emplace_back("policy", ctx.info->access.policy);

		exec_cmd("notify", extras, ctx.info);
		exit(0);
	}
}

void app_socket(const char *socket, const shared_ptr<su_info> &info) {
	vector<Extra> extras;
	extras.reserve(1);
	extras.emplace_back("socket", socket);

	exec_cmd("request", extras, info, PKG_ACTIVITY);
}

void socket_send_request(int fd, const shared_ptr<su_info> &info) {
	write_key_token(fd, "uid", info->uid);
	write_string_be(fd, "eof");
}
