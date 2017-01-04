/* **********************************************************************
* Copyright (C) 2016 Serafin A. Albiero Jr. https://github.com/serajr	*
*									*
*	This program is free software: you can redistribute it and/or	*
*	modify it under the terms of the GNU General Public License as	*
*	published by the Free Software Foundation, either version 3 of	*
*	the License, or (at your option) any later version.		*
*									*
*	This program is distributed in the hope that it will be useful,	*
*	but WITHOUT ANY WARRANTY; without even the implied warranty of	*
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	*
*	GNU General Public License for more details.			*
*									*
*	You should have received a copy of the GNU General Public	*
*	License along with this program.  If not, see			*
*	<http://www.gnu.org/licenses/>.					*
************************************************************************/

/* **********************************************************************
* original source:							*
* https://forum.xda-developers.com/showpost.php?p=69732489&postcount=12 *
************************************************************************/


#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <selinux/android.h>
//#include <selinux/selinux.h>
#include <sys/capability.h>
#include <private/android_filesystem_config.h>
#include <packagelistparser/packagelistparser.h>
#include <paths.h>

static bool packagelist_parse_callback(pkg_info* this_package, void* userdata) {
  pkg_info* p = reinterpret_cast<pkg_info*>(userdata);
  if (strcmp(p->name, this_package->name) == 0) {
    *p = *this_package;
    return false; // stop searching.
  }
  packagelist_free(this_package);
  return true; // keep searching.
}

static void mycapset()
{
  struct __user_cap_header_struct capheader;
  struct __user_cap_data_struct capdata[2];
  memset(&capheader, 0, sizeof(capheader));
  memset(&capdata, 0, sizeof(capdata));
  capheader.version = _LINUX_CAPABILITY_VERSION_3;
  capdata[CAP_TO_INDEX(CAP_SETUID)].effective |= CAP_TO_MASK(CAP_SETUID);
  capdata[CAP_TO_INDEX(CAP_SETGID)].effective |= CAP_TO_MASK(CAP_SETGID);
  capdata[CAP_TO_INDEX(CAP_SETUID)].permitted |= CAP_TO_MASK(CAP_SETUID);
  capdata[CAP_TO_INDEX(CAP_SETGID)].permitted |= CAP_TO_MASK(CAP_SETGID);
  if (capset(&capheader, &capdata[0]) < 0) {
    printf("Could not set capabilities: %s\n", strerror(errno));
  }
}

static void myresset()
{
  if(setresgid(0,0,0) || setresuid(0,0,0)) {
    printf("setresgid/setresuid failed\n");
  }
  printf("running as uid %d\n", getuid());
}

static void usage()
{
	printf("usage:\n");
	printf("  run-as con <context-name>\n-OR-\n");
	printf("  run-as pkg <package-name>\n");
}

int main(int argc, char **argv)
{
  if (argc < 2) {
	usage();
	return 1;
  }

  char* cmdname = argv[1];

  if (strcmp(cmdname, "pkg") == 0) {
	  if (argc < 3) {
		  usage();
		  return 1;
	  }

	  char* pkgname = argv[2];

	  //switch egid so we can read the file.
	  gid_t old_egid = getegid();
	  if (setegid(AID_PACKAGE_INFO) == -1) {
		printf("setegid(AID_PACKAGE_INFO) failed");
	  }

	  // retrieve package information from system
	  pkg_info info;
	  memset(&info, 0, sizeof(info));
	  info.name = pkgname;

	  if (!packagelist_parse(packagelist_parse_callback, &info)) {
		printf("packagelist_parse failed");
	  }
	  if (info.uid == 0) {
		printf("unknown package: %s", pkgname);
	  }

	  mycapset();
	  myresset();

	  uid_t uid = info.uid;
	  if (selinux_android_setcontext(uid, 0, info.seinfo, pkgname) < 0) {
		printf("couldn't set SELinux security context");
	  }

	  printf("done");
  }

  else if (strcmp(cmdname, "con") == 0) {

	  const char *conname;
	  if (argc < 3) {
		  conname = "u:r:shell:s0";
	  }
	  else {
		  conname = argv[2];
	  }

	  mycapset();
	  myresset();

	  int result = setcon(conname);
	  if (result) {
		  printf("Unable to set context to '%s'!\n", conname);
	  }

	  printf("done");
  }

  else {
	usage();
	return 1;
  }

  execlp(_PATH_BSHELL, "sh", NULL);
  return 0;
}
