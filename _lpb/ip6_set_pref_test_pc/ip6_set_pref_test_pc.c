/* confdefs.h */
#define PACKAGE_NAME "Lustre"
#define PACKAGE_TARNAME "lustre"
#define PACKAGE_VERSION "2.15.58_129_gd7d1644"
#define PACKAGE_STRING "Lustre 2.15.58_129_gd7d1644"
#define PACKAGE_BUGREPORT "https://jira.whamcloud.com/"
#define PACKAGE_URL ""
#define PACKAGE "lustre"
#define VERSION "2.15.58_129_gd7d1644"
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define STDC_HEADERS 1
#define SIZEOF_UNSIGNED_LONG_LONG 8
#define HAVE_DLFCN_H 1
#define LT_OBJDIR ".libs/"
#define LUSTRE_MAJOR 2
#define LUSTRE_MINOR 15
#define LUSTRE_PATCH 58
#define LUSTRE_FIX 129
#define LUSTRE_VERSION_STRING "2.15.58_129_gd7d1644"
#define HAVE_PYTHON "3.10"
#define HAVE_MODULE_LOADING_SUPPORT 1
#define HAVE_KSET_FIND_OBJ 1
#define HAVE_GENRADIX_SUPPORT 1
#define HAVE_KFILND 1
#define CONFIG_LUSTRE_FS_PINGER 1
#define CONFIG_ENABLE_CHECKSUM 1
#define CONFIG_ENABLE_FLOCK 1
#define HAVE_LRU_RESIZE_SUPPORT 1
#define HAVE_LIBKEYUTILS 1
#define HAVE_GSS_KEYRING 1
#define HAVE_NAME_TO_HANDLE_AT 1
#define HAVE_FHANDLE_GLIBC_SUPPORT 1
#define HAVE_COPY_FILE_RANGE 1
#define HAVE_COPY_FILE_RANGE 1
#define HAVE_OPENSSL_GETSEPOL 1
#define HAVE_WAIT_BIT_HEADER_H 1
#define HAVE_LINUX_BLK_INTEGRITY_HEADER 1

#include <linux/kernel.h>
#include <linux/module.h>

#if defined(NEED_LOCKDEP_IS_HELD_DISCARD_CONST)  && defined(CONFIG_LOCKDEP)  && defined(lockdep_is_held)
#undef lockdep_is_held
	#define lockdep_is_held(lock) 		lock_is_held((struct lockdep_map *)&(lock)->dep_map)
#endif


		#include <net/ipv6.h>

int
main (void)
{

		ip6_sock_set_addr_preferences(NULL, 0);

  ;
  return 0;
};
MODULE_LICENSE("GPL");
