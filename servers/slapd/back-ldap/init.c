/* init.c - initialize ldap backend */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2003 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/* This is an altered version */
/*
 * Copyright 1999, Howard Chu, All rights reserved. <hyc@highlandsun.com>
 * 
 * Permission is granted to anyone to use this software for any purpose
 * on any computer system, and to alter it and redistribute it, subject
 * to the following restrictions:
 * 
 * 1. The author is not responsible for the consequences of use of this
 *    software, no matter how awful, even if they arise from flaws in it.
 * 
 * 2. The origin of this software must not be misrepresented, either by
 *    explicit claim or by omission.  Since few users ever read sources,
 *    credits should appear in the documentation.
 * 
 * 3. Altered versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.  Since few users
 *    ever read sources, credits should appear in the documentation.
 * 
 * 4. This notice may not be removed or altered.
 *
 *
 *
 * Copyright 2000, Pierangelo Masarati, All rights reserved. <ando@sys-net.it>
 * 
 * This software is being modified by Pierangelo Masarati.
 * The previously reported conditions apply to the modified code as well.
 * Changes in the original code are highlighted where required.
 * Credits for the original code go to the author, Howard Chu.
 */

#include "portable.h"

#include <stdio.h>

#include <ac/socket.h>

#include "slap.h"
#include "back-ldap.h"

#ifdef SLAPD_LDAP_DYNAMIC

int back_ldap_LTX_init_module(int argc, char *argv[]) {
    BackendInfo bi;

    memset( &bi, '\0', sizeof(bi) );
    bi.bi_type = "ldap";
    bi.bi_init = ldap_back_initialize;

    backend_add(&bi);
    return 0;
}

#endif /* SLAPD_LDAP_DYNAMIC */

int
ldap_back_initialize(
    BackendInfo	*bi
)
{
	bi->bi_controls = slap_known_controls;

	bi->bi_open = 0;
	bi->bi_config = 0;
	bi->bi_close = 0;
	bi->bi_destroy = 0;

	bi->bi_db_init = ldap_back_db_init;
	bi->bi_db_config = ldap_back_db_config;
	bi->bi_db_open = 0;
	bi->bi_db_close = 0;
	bi->bi_db_destroy = ldap_back_db_destroy;

	bi->bi_op_bind = ldap_back_bind;
	bi->bi_op_unbind = 0;
	bi->bi_op_search = ldap_back_search;
	bi->bi_op_compare = ldap_back_compare;
	bi->bi_op_modify = ldap_back_modify;
	bi->bi_op_modrdn = ldap_back_modrdn;
	bi->bi_op_add = ldap_back_add;
	bi->bi_op_delete = ldap_back_delete;
	bi->bi_op_abandon = 0;

	bi->bi_extended = ldap_back_extended;

	bi->bi_chk_referrals = 0;
	bi->bi_entry_get_rw = ldap_back_entry_get;

	bi->bi_connection_init = 0;
	bi->bi_connection_destroy = ldap_back_conn_destroy;

	return 0;
}

int
ldap_back_db_init(
    Backend	*be
)
{
	struct ldapinfo	*li;
	struct ldapmapping *mapping;

	li = (struct ldapinfo *) ch_calloc( 1, sizeof(struct ldapinfo) );
	if ( li == NULL ) {
 		return -1;
 	}

	li->binddn.bv_val = NULL;
	li->binddn.bv_len = 0;
	li->bindpw.bv_val = NULL;
	li->bindpw.bv_len = 0;

#ifdef ENABLE_REWRITE
 	li->rwinfo = rewrite_info_init( REWRITE_MODE_USE_DEFAULT );
	if ( li->rwinfo == NULL ) {
 		ch_free( li );
 		return -1;
 	}
#endif /* ENABLE_REWRITE */

	ldap_pvt_thread_mutex_init( &li->conn_mutex );

	ldap_back_map_init( &li->at_map, &mapping );

	li->be = be;
	be->be_private = li;

	return 0;
}

void
ldap_back_conn_free( 
	void *v_lc
)
{
	struct ldapconn *lc = v_lc;
	ldap_unbind( lc->ld );
	if ( lc->bound_dn.bv_val ) {
		ch_free( lc->bound_dn.bv_val );
	}
	if ( lc->cred.bv_val ) {
		ch_free( lc->cred.bv_val );
	}
	if ( lc->local_dn.bv_val ) {
		ch_free( lc->local_dn.bv_val );
	}
	ldap_pvt_thread_mutex_destroy( &lc->lc_mutex );
	ch_free( lc );
}

void
mapping_free( void *v_mapping )
{
	struct ldapmapping *mapping = v_mapping;
	ch_free( mapping->src.bv_val );
	ch_free( mapping->dst.bv_val );
	ch_free( mapping );
}

int
ldap_back_db_destroy(
    Backend	*be
)
{
	struct ldapinfo	*li;

	if (be->be_private) {
		li = (struct ldapinfo *)be->be_private;

		ldap_pvt_thread_mutex_lock( &li->conn_mutex );

		if (li->url) {
			ch_free(li->url);
			li->url = NULL;
		}
		if (li->binddn.bv_val) {
			ch_free(li->binddn.bv_val);
			li->binddn.bv_val = NULL;
		}
		if (li->bindpw.bv_val) {
			ch_free(li->bindpw.bv_val);
			li->bindpw.bv_val = NULL;
		}
                if (li->conntree) {
			avl_free( li->conntree, ldap_back_conn_free );
		}
#ifdef ENABLE_REWRITE
		if (li->rwinfo) {
			rewrite_info_delete( li->rwinfo );
		}
#else /* !ENABLE_REWRITE */
		if (li->suffix_massage) {
  			ber_bvarray_free( li->suffix_massage );
 		}
#endif /* !ENABLE_REWRITE */

		avl_free( li->oc_map.remap, NULL );
		avl_free( li->oc_map.map, mapping_free );
		avl_free( li->at_map.remap, NULL );
		avl_free( li->at_map.map, mapping_free );
		
		ldap_pvt_thread_mutex_unlock( &li->conn_mutex );
		ldap_pvt_thread_mutex_destroy( &li->conn_mutex );
	}

	ch_free( be->be_private );
	return 0;
}
