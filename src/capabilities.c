/*
  Copyright (c) 2012-2016, Matthias Schiffer <mschiffer@universe-factory.net>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
   \file

   Handling of POSIX capabilities
*/


#include "fastd.h"

#ifdef WITH_CAPABILITIES

#include "config.h"

#include <sys/capability.h>
#include <sys/prctl.h>


/** Tries to acquire a capability */
static void try_cap(cap_value_t cap) {
	char *name = cap_to_name(cap);
	if (!name)
		return;

	cap_t caps = cap_get_proc();
	if (!caps)
		goto end_free;

	cap_flag_value_t val;
	if (cap_get_flag(caps, cap, CAP_EFFECTIVE, &val) < 0)
		exit_errno("cap_get_flag");

	if (val == CAP_SET)
		goto end_free;

	pr_verbose("trying to acquire %s", name);

	if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap, CAP_SET) < 0)
		exit_errno("cap_set_flags");

	if (cap_set_proc(caps) < 0) {
		pr_debug_errno("cap_set_proc");
		goto end_free;
	}

	pr_verbose("acquired capability %s", name);

 end_free:
	cap_free(caps);
	cap_free(name);
}

/** Returns true if CAP_NET_ADMIN should be retained */
static bool need_cap_net_admin(void) {
	if (!fastd_config_persistent_ifaces() && conf.drop_caps != DROP_CAPS_FORCE)
		return true;

#ifdef USE_PACKET_MARK
	if (!(ctx.sock_default_v4 || ctx.sock_default_v6) && conf.packet_mark)
		return true;
#endif

	return false;
}

/** Returns true if CAP_NET_RAW should be retained */
static bool need_cap_net_raw(void) {
	if (!ctx.sock_default_v4 && conf.bind_addr_default_v4 && conf.bind_addr_default_v4->bindtodev)
		return true;

	if (!ctx.sock_default_v6 && conf.bind_addr_default_v6 && conf.bind_addr_default_v6->bindtodev)
		return true;

	return false;
}

/** Sets a single capability as permitted and effective in the given cap_t */
static void set_cap(cap_t caps, cap_value_t cap) {
	char *name = cap_to_name(cap);
	if (name) {
		pr_verbose("retaining %s", name);
		cap_free(name);
	}

	if (cap_set_flag(caps, CAP_PERMITTED, 1, &cap, CAP_SET) < 0)
		exit_errno("cap_set_flags");
	if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap, CAP_SET) < 0)
		exit_errno("cap_set_flags");
}

/** Tries to acquire the capabilities needed to perform initialization without root privileges */
void fastd_cap_acquire(void) {
	/* interface creation */
	try_cap(CAP_NET_ADMIN);

	/* privileged binds */
	try_cap(CAP_NET_BIND_SERVICE);

	/* device binds */
	try_cap(CAP_NET_RAW);

	if (prctl(PR_SET_KEEPCAPS, 1) < 0)
		pr_warn_errno("prctl(PR_SET_KEEPCAPS)");
}

/** Reacquires required capabilities, drops the rest */
void fastd_cap_reacquire_drop(void) {
	cap_t caps = cap_init();

	if (need_cap_net_admin())
		set_cap(caps, CAP_NET_ADMIN);

	if (need_cap_net_raw())
		set_cap(caps, CAP_NET_RAW);

	if (cap_set_proc(caps) < 0)
		exit_errno("unable to retain required capabilities");
	else
		pr_verbose("dropped capabilities");

	cap_free(caps);
}

#endif
