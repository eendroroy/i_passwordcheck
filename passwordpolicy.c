/*-------------------------------------------------------------------------
 *
 * passwordpolicy.c
 *
 * Copyright (c) 2018, indrajit
 *
 * Created from postgres passwordcheck.
 *
 *
 * Original license:
 *
 *
 *  /*-------------------------------------------------------------------------
 *  *
 *  * passwordcheck.c
 *  *
 *  *
 *  * Copyright (c) 2009-2017, PostgreSQL Global Development Group
 *  *
 *  * Author: Laurenz Albe <laurenz.albe@wien.gv.at>
 *  *
 *  * IDENTIFICATION
 *  *	  contrib/passwordcheck/passwordcheck.c
 *  *
 *  *-------------------------------------------------------------------------
 *  *\/
 *
 *-------------------------------------------------------------------------
 */

#include <ctype.h>
#include "postgres.h"
#include "catalog/namespace.h"
#include "utils/guc.h"
#include "commands/user.h"
#include "libpq/crypt.h"
#include "fmgr.h"

#if PG_VERSION_NUM < 100000
#include "libpq/md5.h"
#endif

#ifdef USE_CRACKLIB
#include <crack.h>
#endif

PG_MODULE_MAGIC;

extern void _PG_init(void);

// p_policy.min_password_len
int passMinLength = 8;

// p_policy.min_special_chars
int passMinSpcChar = 2;

// p_policy.min_numbers
int passMinNumChar = 2;

// p_policy.min_uppercase_letter
int passMinUpperChar = 2;

// p_policy.min_lowercase_letter
int passMinLowerChar = 2;

/*
 * check_password
 *
 * performs checks on an encrypted or unencrypted password
 * ereport's if not acceptable
 *
 * username: name of role being created or changed
 * password: new password (possibly already encrypted)
 * password_type: PASSWORD_TYPE_PLAINTEXT or PASSWORD_TYPE_MD5 (there
 *			could be other encryption schemes in future)
 * validuntil_time: password expiration time, as a timestamptz Datum
 * validuntil_null: true if password expiration time is NULL
 *
 * This sample implementation doesn't pay any attention to the password
 * expiration time, but you might wish to insist that it be non-null and
 * not too far in the future.
 */

static void check_policy(const char *password) {
  int i, pwdlen, letter_count, number_count, spc_char_count, upper_count, lower_count;

  pwdlen = strlen(password);

  letter_count = 0;
  number_count = 0;
  spc_char_count = 0;
  upper_count = 0;
  lower_count = 0;

  for (i = 0; i < pwdlen; i++) {
    /*
     * isalpha() does not work for multibyte encodings but let's
     * consider non-ASCII characters non-letters
     */
    if (isalpha((unsigned char)password[i])) {
      letter_count++;
      if (isupper((unsigned char)password[i])) {
        upper_count++;
      } else if (islower((unsigned char)password[i])) {
        lower_count++;
      }
    } else if (isdigit((unsigned char)password[i])) {
      number_count++;
    } else {
      spc_char_count++;
    }
  }
  if (number_count < passMinNumChar) {
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg("password must contain atleast %d numeric characters.",
                    passMinNumChar)));
  } else if (spc_char_count < passMinSpcChar) {
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg("password must contain atleast %d special characters.",
                    passMinSpcChar)));
  } else if (upper_count < passMinUpperChar) {
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg("password must contain atleast %d upper case letters.",
                    passMinUpperChar)));
  } else if (lower_count < passMinLowerChar) {
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg("password must contain atleast %d lower case letters.",
                    passMinLowerChar)));
  }
}

#if PG_VERSION_NUM >= 100000
static void check_password(const char *username, const char *shadow_pass,
                           PasswordType password_type, Datum validuntil_time,
                           bool validuntil_null) {
  if (password_type != PASSWORD_TYPE_PLAINTEXT) {
    /*
     * Unfortunately we cannot perform exhaustive checks on encrypted
     * passwords - we are restricted to guessing. (Alternatively, we could
     * insist on the password being presented non-encrypted, but that has
     * its own security disadvantages.)
     *
     * We only check for username = password.
     */
    char *logdetail;

    if (plain_crypt_verify(username, shadow_pass, username, &logdetail) == STATUS_OK) {
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                      errmsg("password must not contain user name")));
    }
  } else {
    /*
     * For unencrypted passwords we can perform better checks
     */
    const char *password = shadow_pass;
    int pwdlen = strlen(password);

    /* enforce minimum length */
    if (pwdlen < passMinLength) {
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                      errmsg("password is too short.")));
    }

    /* check if the password contains the username */
    if (strstr(password, username)) {
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                      errmsg("password must not contain user name.")));
    }

    check_policy(password);

#ifdef USE_CRACKLIB
    /* call cracklib to check password */
    if (FascistCheck(password, CRACKLIB_DICTPATH)) {
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                      errmsg("password is easily cracked.")));
    }
#endif
  }

  /* all checks passed, password is ok */
}
#else
static void check_password(const char *username, const char *password,
                           int password_type, Datum validuntil_time,
                           bool validuntil_null) {
  int namelen = strlen(username);
  int pwdlen = strlen(password);
  char encrypted[MD5_PASSWD_LEN + 1];

  switch (password_type) {
  case PASSWORD_TYPE_MD5:

    /*
     * Unfortunately we cannot perform exhaustive checks on encrypted
     * passwords - we are restricted to guessing. (Alternatively, we
     * could insist on the password being presented non-encrypted, but
     * that has its own security disadvantages.)
     *
     * We only check for username = password.
     */
    if (!pg_md5_encrypt(username, username, namelen, encrypted)) {
      elog(ERROR, "password encryption failed.");
    }
    if (strcmp(password, encrypted) == 0) {
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                      errmsg("password must not contain user name.")));
    }
    break;

  case PASSWORD_TYPE_PLAINTEXT:

    /*
     * For unencrypted passwords we can perform better checks
     */

    /* enforce minimum length */
    if (pwdlen < passMinLength) {
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                      errmsg("password is too short.")));
    }

    /* check if the password contains the username */
    if (strstr(password, username)) {
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                      errmsg("password must not contain user name.")));
    }

    check_policy(password);

#ifdef USE_CRACKLIB
    /* call cracklib to check password */
    if (FascistCheck(password, CRACKLIB_DICTPATH)) {
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                      errmsg("password is easily cracked.")));
    }
#endif
    break;

  default:
    elog(ERROR, "unrecognized password type: %d.", password_type);
    break;
  }

  /* all checks passed, password is ok */
}

#endif

static void define_variables() {
  /* Define p_policy.min_pass_len */
  DefineCustomIntVariable("p_policy.min_password_len",
                          "Minimum password length.", NULL, &passMinLength, 8,
                          1, INT_MAX, PGC_SIGHUP, 0, NULL, NULL, NULL);

  /* Define p_policy.min_special_chars */
  DefineCustomIntVariable(
      "p_policy.min_special_chars", "Minimum number of special characters.",
      NULL, &passMinSpcChar, 2, 1, INT_MAX, PGC_SIGHUP, 0, NULL, NULL, NULL);

  /* Define p_policy.min_numbers */
  DefineCustomIntVariable(
      "p_policy.min_numbers", "Minimum number of numeric characters.", NULL,
      &passMinNumChar, 2, 1, INT_MAX, PGC_SIGHUP, 0, NULL, NULL, NULL);

  /* Define p_policy.min_uppercase_letter */
  DefineCustomIntVariable(
      "p_policy.min_uppercase_letter", "Minimum number of upper case letters.",
      NULL, &passMinUpperChar, 2, 1, INT_MAX, PGC_SIGHUP, 0, NULL, NULL, NULL);

  /* Define p_policy.min_lowercase_letter */
  DefineCustomIntVariable(
      "p_policy.min_lowercase_letter", "Minimum number of lower case letters.",
      NULL, &passMinLowerChar, 2, 1, INT_MAX, PGC_SIGHUP, 0, NULL, NULL, NULL);

  if (passMinLength < (passMinSpcChar + passMinNumChar + passMinUpperChar + passMinLowerChar)) {
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("configuration error.\nsum of minimum character "
                           "requirement exceeds minimum password length.")));
  }
}

/*
 * Module initialization function
 */
void _PG_init(void) {
  /* Be sure we do initialization only once */
  static bool inited = false;

  if (inited) {
    return;
  }

  define_variables();

  /* activate password checks when the module is loaded */
  check_password_hook = check_password;

  inited = true;
}
