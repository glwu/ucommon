// Copyright (C) 2010 David Sugar, Tycho Softworks.
//
// This file is part of GNU uCommon C++.
//
// GNU uCommon C++ is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published 
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// GNU uCommon C++ is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with GNU uCommon C++.  If not, see <http://www.gnu.org/licenses/>.

#include "local.h"

extern "C" {
#ifndef	_MSWINDOWS_
	static int gcrypt_mutex_init(void **mp)
	{
		if(mp)
			*mp = new mutex();
		return 0;
	}

	static int gcrypt_mutex_destroy(void **mp)
	{
		if(mp && *mp)
			delete (mutex *)(*mp);
		return 0;
	}

	static int gcrypt_mutex_lock(void **mp)
	{
		mutex *m = (mutex *)(*mp);
		m->acquire();
		return 0;
	}

	static int gcrypt_mutex_unlock(void **mp)
	{
		mutex *m = (mutex *)(*mp);
		m->release();
		return 0;
	}

	static struct gcry_thread_cbs gcrypt_threading = {
		GCRY_THREAD_OPTION_PTHREAD, NULL,
	    gcrypt_mutex_init, gcrypt_mutex_destroy,
		gcrypt_mutex_lock, gcrypt_mutex_unlock
    };
#endif

	static void secure_shutdown(void)
	{
		gnutls_global_deinit();
	}
}

static const char *certid = "generic";

gnutls_priority_t context::priority_cache;

bool secure::init(const char *progname)
{
	static bool initialized = false;
	
	if(!initialized) {
		Thread::init();
		if(progname) {
			Socket::init(progname);
			certid = progname;
		}
		else
			Socket::init();

#ifndef	_MSWINDOWS_
		gcry_control(GCRYCTL_SET_THREAD_CBS, &gcrypt_threading);
#endif

		gnutls_global_init();
		gnutls_priority_init (&context::priority_cache, "NORMAL", NULL);
		atexit(secure_shutdown);
		initialized = true;
	}
	return true;
}

secure::context_t secure::server(const char *ca)
{
	char certfile[256];
	char keyfile[256];

	context *ctx = new context;

	if(!ctx)
		return NULL;

	ctx->error = secure::OK;
	ctx->connect = GNUTLS_SERVER;
	ctx->xtype = GNUTLS_CRD_CERTIFICATE;
	ctx->xcred = NULL;
	ctx->dh = NULL;
	gnutls_certificate_allocate_credentials(&ctx->xcred);

	snprintf(certfile, sizeof(certfile), "%s/%s.pem", SSL_CERTS, certid);
	snprintf(keyfile, sizeof(keyfile), "%s/%s.pem", SSL_PRIVATE, certid);
	gnutls_certificate_set_x509_key_file(ctx->xcred, certfile, keyfile, GNUTLS_X509_FMT_PEM);

	if(!ca)
		return ctx;

	if(eq(ca, "*"))
		ca = SSL_CERTS;
	else if(*ca != '/') {
		snprintf(certfile, sizeof(certfile), "%s/%s.pem", SSL_CERTS, ca);
		ca = certfile;
	}

	gnutls_certificate_set_x509_trust_file (ctx->xcred, certfile, GNUTLS_X509_FMT_PEM);

	return ctx;
}

secure::context_t secure::client(const char *ca)
{
	char certfile[256];

	context *ctx = new context;

	if(!ctx)
		return NULL;

	ctx->error = secure::OK;
	ctx->connect = GNUTLS_CLIENT;
	ctx->xtype = GNUTLS_CRD_CERTIFICATE;
	ctx->xcred = NULL;
	ctx->dh = NULL;
	gnutls_certificate_allocate_credentials(&ctx->xcred);

	if(!ca)
		return ctx;

	if(eq(ca, "*"))
		ca = SSL_CERTS;
	else if(*ca != '/') {
		snprintf(certfile, sizeof(certfile), "%s/%s.pem", SSL_CERTS, ca);
		ca = certfile;
	}

	gnutls_certificate_set_x509_trust_file (ctx->xcred, certfile, GNUTLS_X509_FMT_PEM);

	return ctx;
}

context::~context()
{
	if(dh)
		gnutls_dh_params_deinit(dh);

	if(!xcred)
		return;

	switch(xtype) {
	case GNUTLS_CRD_ANON:
		gnutls_anon_free_client_credentials((gnutls_anon_client_credentials_t)xcred);
		break;
	case GNUTLS_CRD_CERTIFICATE:
		gnutls_certificate_free_credentials(xcred);
		break;
	default:
		break;
	}
}

secure::~secure()
{
}

void secure::uuid(char *str)
{
	static unsigned char buf[16];
	static Timer::tick_t prior = 0l;
	static unsigned short seq;
	Timer::tick_t current = Timer::ticks();

	Mutex::protect(&buf);

	// get our (random) node identifier...
	if(!prior)	
		Random::fill(buf + 10, 6);
	
	if(current == prior)
		++seq;
	else
		Random::fill((unsigned char *)&seq, sizeof(seq));

	buf[8] = (seq >> 8) & 0xff;
	buf[9] = seq & 0xff;
	buf[3] = current & 0xff;
	buf[2] = (current >> 8) & 0xff;
	buf[1] = (current >> 16) & 0xff;
	buf[0] = (current >> 24) & 0xff;
	buf[5] = (current >> 32) & 0xff;
	buf[4] = (current >> 40) & 0xff;
	buf[7] = (current >> 48) & 0xff;
	buf[6] = (current >> 56) & 0xff;

	buf[6] &= 0x0f;
	buf[6] |= 0x10;
	buf[8] |= 0x80;
	String::hexdump(buf, str, "4-2-2-2-6");
	Mutex::release(&buf);
}

String secure::uuid(void)
{
	char buf[38];
	uuid(buf);
	return String(buf);
}


