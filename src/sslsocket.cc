/**
 * @file src/sslsocket.cc
 * SSLSocket class
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include<iostream>
#include<sstream>
#include<vector>
#include<map>

#include<openssl/ui.h>
#include<openssl/err.h>
#include<openssl/engine.h>
#include<openssl/crypto.h>

#include"sslsocket.h"
#include"util2.h"
#include"tlssh.h"

#if 0
// do I need this somewhere?
	char *randfile = "random.seed";
	int fd;
	RAND_load_file("/dev/urandom", 1024);

	unlink(randfile);
	fd = open(randfile, O_WRONLY | O_CREAT | O_EXCL, 0600);
	close(fd);
	RAND_write_file("random.seed");
#endif

#if 1
#define SSLCALL(x) x
#else
#define SSLCALL(x) (printf("%s\n", #x), x)
#endif

/**
 * create a new X509 wrapper object. Null pointer not allowed.
 */
X509Wrap::X509Wrap(X509 *x509)
	:x509(x509)
{
	if (!x509) {
                THROW(ErrBase, "X509Wrap inited with null pointer");
	}
}

/**
 * check 'host' against all subjectAltNames and the CN subject
 * name. If any of them match, return true. Else false.
 */
bool
X509Wrap::check_hostname(const std::string &host)
{
	int extcount;
	int i, j;

	// check X509v3 extensions
	extcount = X509_get_ext_count(x509);
	for (i = 0; i < extcount; i++) {
		X509_EXTENSION *ext;
		const char *extstr;

                ext = SSLCALL(X509_get_ext(x509, i));
                extstr = SSLCALL(OBJ_nid2sn(SSLCALL(OBJ_obj2nid(SSLCALL(X509_EXTENSION_get_object(ext))))));
		if (!strcmp(extstr, "subjectAltName")) {
			X509V3_EXT_METHOD *meth;
			const unsigned char *data;
			STACK_OF(CONF_VALUE) *val;
			CONF_VALUE *nval;

                        meth = SSLCALL(X509V3_EXT_get(ext));
			if (!meth) {
				continue;
			}
			if (!meth->d2i) {
				logger->info("meth->d2i missing?! FIXME");
				continue;
			}
			data = ext->value->data;
			val = meth->i2v(meth,
					meth->d2i(NULL,
						  &data,
						  ext->value->length),
					NULL);
			for (j = 0; j < sk_CONF_VALUE_num(val); j++) {
				nval = sk_CONF_VALUE_value(val, j);
				if (!strcmp(nval->name, "DNS")
				    && !strcmp(nval->value,
					       host.c_str())) {
					return true;
				}
			}
		}
	}

	// check subject name
	X509_NAME *subj;
	char sdata[256];
        subj = SSLCALL(X509_get_subject_name(x509));
	if (!subj) {
		return false;
	}
        if (!SSLCALL(X509_NAME_get_text_by_NID(subj, NID_commonName,
                                               sdata, sizeof(sdata)))) {
		return false;
	}
	sdata[sizeof(sdata) - 1] = 0;
	if (!strcmp(sdata, host.c_str())) {
		return true;
	}

	// default: name does not match
	return false;
}

/**
 * get Common Name of cert
 */
std::string
X509Wrap::get_common_name() const
{
	char buf[1024];
	X509_NAME *subj;

        subj = SSLCALL(X509_get_subject_name(x509));
	if (!subj) {
                THROW(ErrSSL, "X509_get_subject_name()");
	}
        if (!SSLCALL(X509_NAME_get_text_by_NID(subj, NID_commonName,
                                               buf, sizeof(buf)))) {
                THROW(ErrSSL, "X509_NAME_get_text_by_NID()");
	}
	buf[sizeof(buf) - 1] = 0;
	return std::string(buf);
}

/**
 * get issuer of cert
 */
std::string
X509Wrap::get_issuer() const
{
	char buf[1024];
        SSLCALL(X509_NAME_oneline(X509_get_issuer_name(x509),
                                  buf, sizeof(buf)));
	return std::string(buf);
}

/**
 * get the common name of the issuer of the cert
 */
std::string
X509Wrap::get_issuer_common_name() const
{
	char buf[1024];
        X509_NAME *issuer;

        issuer = SSLCALL(X509_get_issuer_name(x509));
        if (!issuer) {
                THROW(ErrSSL, "X509_get_issuer_name()");
        }

        if (!SSLCALL(X509_NAME_get_text_by_NID(issuer, NID_commonName,
                                               buf, sizeof(buf)))) {
                THROW(ErrSSL, "X509_NAME_get_text_by_NID()");
	}
	buf[sizeof(buf) - 1] = 0;
	return std::string(buf);
}

/**
 * get subject name only (no /CN or anything). E.g. bob.users.domain.com
 */
std::string
X509Wrap::get_subject() const
{
	char buf[1024];
        SSLCALL(X509_NAME_oneline(X509_get_subject_name(x509),
                                  buf, sizeof(buf)));
	return std::string(buf);
}

/**
 *
 */
std::string
X509Wrap::get_fingerprint() const
{
        const EVP_MD *md = SSLCALL(EVP_get_digestbyname("SHA1"));
        if (!md) {
                THROW(ErrSSL, "EVP_get_digestbyname()");
        }

        unsigned char buf[1024];
        unsigned int len = 0;
        int n = SSLCALL(X509_digest(x509, md, buf, &len));
        if (n != 1 || len != 20) {
                THROW(ErrSSL,
                      xsprintf("X509_digest() failed (ret=%d, len=%d)",
                               n, len));
        }

        std::string ret;
        unsigned int c;
        for (c = 0; c < len; c++) {
                if (c) {
                        ret += ":";
                }
                ret += xsprintf("%.2X", buf[c]);
        }
        return ret;
}

/**
 * X509Wrap destructor
 */
X509Wrap::~X509Wrap()
{
	if (x509) {
		X509_free(x509);
		x509 = 0;
	}
}

/**
 *
 */
X509Wrap::ErrSSL::ErrSSL(const Err::ErrData &errdata, const std::string &m,
                         SSL *ssl, int err)
	:ErrBase(errdata, m)
{
	if (ssl) {
		sslmsg = SSLSocket
			::ssl_errstr(SSL_get_error(ssl, err));
                msg = msg + ": " + sslmsg;
	}
}

void
SSLSocket::global_init()
{
        static bool inited = false;
        if (inited) {
                return;
        }
        SSLCALL(SSL_library_init());
        SSLCALL(OpenSSL_add_all_algorithms());
        SSLCALL(SSL_load_error_strings());
        SSLCALL(ERR_load_SSL_strings());
        make_thread_safe();
        inited = true;
}

/**
 * FIXME: instead use CRYPTO_THREADID functions where available.
 */
void
SSLSocket::make_thread_safe()
{
        SSLCALL(CRYPTO_set_id_callback(threadid_callback));
        SSLCALL(CRYPTO_set_locking_callback(locking_callback));
}

/**
 * Set up OpenSSL stuff
 */
SSLSocket::SSLSocket(int fd)
                :Socket(fd),
                 ctx(NULL),
                 ssl(NULL),
                 privkey_engine_(  std::make_pair(false, "")),
                 privkey_password_(std::make_pair(false, "")),
                 tpm_srk_password_(std::make_pair(false, ""))
{
        global_init();
}

/**
 * get a new X509 object that represents the cert
 *
 * @return the cert
 */
std::auto_ptr<X509Wrap>
SSLSocket::get_cert()
{
        return std::auto_ptr<X509Wrap>
                (new X509Wrap(SSLCALL(SSL_get_peer_certificate(ssl))));
}

/**
 * Return error description for X509 errors.
 *
 * @todo there must be a built-in library function for this
 */
const std::string
X509Wrap::errstr(int err)
{
        switch (err) {
        case X509_V_OK:
                return "ok";
        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
                return "unable to get issuer";
        case X509_V_ERR_UNABLE_TO_GET_CRL:
                return "unable to get certificate CRL";
        case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE:
                return "unable to decrypt";
        case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
                return "unable to decrypt CRL’s";
        case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
                return "unable to decode";
        case X509_V_ERR_CERT_SIGNATURE_FAILURE:
                return "certificate signature failure";
        case X509_V_ERR_CRL_SIGNATURE_FAILURE:
                return "CRL signature failure";
        case X509_V_ERR_CERT_NOT_YET_VALID:
                return "certificate is not yet valid";
        case X509_V_ERR_CERT_HAS_EXPIRED:
                return "certificate has expired";
        case X509_V_ERR_CRL_NOT_YET_VALID:
                return "CRL is not yet valid";
        case X509_V_ERR_CRL_HAS_EXPIRED:
                return "CRL has expired";
        case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
                return "format error in";
        case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
                return "format error in";
        case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD:
                return "format error in CRL’s";
        case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
                return "format error in CRL’s";
        case X509_V_ERR_OUT_OF_MEM:
                return "out of memory";
        case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
                return "self signed certificate";
        case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                return "self signed certificate in";
        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
                return "unable to get local";
        case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
                return "unable to verify the";
        default:
                return "unknown X509 error";
        }
}

/**
 * Return error description for SSL errors.
 *
 * @todo there must be a built-in library function for this
 */
const std::string
SSLSocket::ssl_errstr(int err)
{
	switch (err) {
	case SSL_ERROR_NONE:
		return "None";
	case SSL_ERROR_ZERO_RETURN:
		return "zero return";
	case SSL_ERROR_WANT_READ:
		return "want read";
	case SSL_ERROR_WANT_WRITE:
		return "want write";
	case SSL_ERROR_WANT_CONNECT:
		return "want connect";
	case SSL_ERROR_WANT_ACCEPT:
		return "want accept";
	case SSL_ERROR_WANT_X509_LOOKUP:
		return "x509 lookup";
	case SSL_ERROR_SYSCALL:
		return "syscall";
	case SSL_ERROR_SSL:
		return "SSL_ERROR_SSL";
	}
	return "uhh.. what?";
}

/**
 * destroy SSLSocket object
 */
SSLSocket::~SSLSocket()
{
        shutdown();
}

/**
 * undo everything that ssl_connect()/ssl_accept() did
 */
void
SSLSocket::shutdown()
{
	if (ssl) {
                SSLCALL(SSL_shutdown(ssl));
                SSLCALL(SSL_free(ssl));
		ssl = 0;
	}
        if (ctx) {
                SSLCALL(SSL_CTX_free(ctx));
                ctx = 0;
        }
        close();
}

/**
 * make this sslsocket take over the fd of a normal socket object
 */
void
SSLSocket::ssl_attach(Socket &sock)
{
	fd.set(sock.getfd());
	sock.forget();
}

/**
 * start handshake as SSL client
 */
void
SSLSocket::ssl_connect(const std::string &inhost)
{
	host = inhost;
	ssl_accept_connect(true);
}

/**
 * start handshake as SSL server
 */
void
SSLSocket::ssl_accept()
{
	ssl_accept_connect(false);
}

/**
 * setup Diffie-Hellman parameters
 */
DH*
SSLSocket::ssl_setup_dh()
{
        DH* dh = DH_new();
        if (!dh) {
                THROW(ErrSSL, "DH_new()");
        }

        if (!DH_generate_parameters_ex(dh, 2, DH_GENERATOR_2, 0)) {
                THROW(ErrSSL, "DH_generate_parameters_ex()");
        }

        int codes = 0;
        if (!DH_check(dh, &codes) && !codes) {
                THROW(ErrSSL, "DH_check()");
        }

        if (!DH_generate_key(dh)) {
                THROW(ErrSSL, "DH_generate_key()");
        }

        return dh;
}


SSLSocket::Engine::Engine(const std::string &id)
        :engine_(NULL),id_(id)
{
        if (!(engine_ = ENGINE_by_id(id_.c_str()))) {
                THROW(ErrSSL, "ENGINE_by_id()");
        }
        if (!ENGINE_init(engine_)) {
                ENGINE_free(engine_);
                THROW(ErrSSL, "ENGINE_init()");
        }
}

SSLSocket::Engine::~Engine()
{
        if (engine_) {
                ENGINE_free(engine_);
        }
}

/**
 * FIXME: this should not be global.
 */
struct OpensslCbData
{
        typedef std::map<std::string, std::string> qa_t;
        qa_t qa;
};
static OpensslCbData openssl_cb_data;

/**
 *
 */
static int
ui_open(UI *ui)
{
        OpensslCbData *cb_data(&openssl_cb_data);
        return UI_method_get_opener(UI_OpenSSL())(ui);
}

/**
 *
 */
static int
ui_read(UI *ui, UI_STRING *uis)
{
        OpensslCbData *cb_data(&openssl_cb_data);

        OpensslCbData::qa_t::const_iterator itr;
        itr = cb_data->qa.find(UI_get0_output_string(uis));
        if (itr != cb_data->qa.end()) {
                UI_set_result(ui, uis, itr->second.c_str());
                return 1;
        }
        UI_method_get_writer(UI_OpenSSL())(ui, uis);
        return UI_method_get_reader(UI_OpenSSL())(ui, uis);
}

/**
 *
 */
static int
ui_write(UI *ui, UI_STRING *uis)
{
        OpensslCbData *cb_data(&openssl_cb_data);
        return 1;
}

/**
 *
 */
static int
ui_close(UI *ui)
{
        OpensslCbData *cb_data(&openssl_cb_data);
        return UI_method_get_closer(UI_OpenSSL())(ui);
}

/**
 *
 */
static UI_METHOD*
setup_ui_method(void)
{
        UI_METHOD *ui_method;
        ui_method = UI_create_method((char*)"OpenSSL application user interface");
        UI_method_set_opener(ui_method, ui_open);
        UI_method_set_reader(ui_method, ui_read);
        UI_method_set_writer(ui_method, ui_write);
        UI_method_set_closer(ui_method, ui_close);
        return ui_method;
}

/**
 *
 */
EVP_PKEY*
SSLSocket::Engine::LoadPrivKey(const std::string &fn)
{
        EVP_PKEY *pkey;

        // FIXME: callback data doesn't seem to actually be sent on to the callback
        //        functions.
        // OpensslCbData cb_data;
        if (tpm_srk_password_.first) {
                openssl_cb_data.qa["SRK authorization: "] = tpm_srk_password_.second;
        }
        if (privkey_password_.first) {
                openssl_cb_data.qa["TPM Key Password: "] = privkey_password_.second;
        }

        UI_METHOD *ui_method = setup_ui_method();
        pkey = ENGINE_load_private_key(engine_,
                                       fn.c_str(),
                                       ui_method,
                                       &openssl_cb_data);
        if (!pkey) {
                THROW(ErrSSL, "ENGINE_load_private_key()");
        }
        return pkey;
}
/**
 * combinded server and client handshake code.
 *
 * @param[in] isconnect  true if we are client, false if we are server
 */
void
SSLSocket::ssl_accept_connect(bool isconnect)
{
	int err;

        // create CTX
        ctx = SSLCALL(SSL_CTX_new(isconnect
                                  ? SSLCALL(TLSv1_client_method())
                                  : SSLCALL(TLSv1_server_method())));
        if (!ctx) {
                THROW(ErrSSL, "SSL_CTX_new()");
	}

        // load cert & key
        if (1 !=SSLCALL(SSL_CTX_use_certificate_chain_file(ctx,
                                                           certfile.c_str()))){
                THROW(ErrSSL, "Load certfile " + certfile);
	}

        if (privkey_engine_.first) {
                // Start TPM engine.
                SSLCALL(ENGINE_load_builtin_engines());

                logger->debug("Loading private key using engine %s",
                              privkey_engine_.second.c_str());
                Engine *engine(new Engine(privkey_engine_.second));
                EVP_PKEY *pkey;
                engine->set_privkey_password(privkey_password_);
                engine->set_tpm_srk_password(tpm_srk_password_);
                pkey = engine->LoadPrivKey(keyfile);
                if (0 > SSLCALL(SSL_CTX_use_PrivateKey(ctx, pkey))) {
                        THROW(ErrSSL, "SSL_CTX_use_PrivateKey()");
                }
        } else {
                logger->debug("Loading private key");
                if (1!=SSLCALL(SSL_CTX_use_PrivateKey_file(ctx,
                                                           keyfile.c_str(),
                                                           SSL_FILETYPE_PEM))){
                        THROW(ErrSSL, "Load keyfile " + keyfile);
                }
        }

        // set CAPath & CAFile for cert verification
	const char *ccapath = capath.c_str();
	const char *ccafile = cafile.c_str();
	if (!*ccafile) {
		ccafile = NULL;
	}
	if (!*ccapath) {
		ccapath = NULL;
	}
	if (ccafile || ccapath) {
		if (debug) {
			logger->debug("CAFile: %s", ccafile?ccafile:"<null>");
		}
                if (!SSLCALL(SSL_CTX_load_verify_locations(ctx,
                                                           ccafile,
                                                           ccapath))) {
                        THROW(ErrSSL, "SSL_CTX_load_verify_locations()");
		}
                SSLCALL(SSL_CTX_set_verify_depth(ctx, 5));
                SSLCALL(SSL_CTX_set_verify(ctx,
                                           SSL_VERIFY_PEER
                                           |(isconnect
                                             ? 0
                                             :SSL_VERIFY_FAIL_IF_NO_PEER_CERT),
                                           NULL));
	}

        // set approved cipher list
        logger->debug("setting up cipher list %s", cipher_list.c_str());
	if (!cipher_list.empty()) {
                if (!SSLCALL(SSL_CTX_set_cipher_list(ctx,
                                                     cipher_list.c_str()))) {
                        THROW(ErrSSL, "SSL_CTX_set_cipher_list()");
                }
        }

        // if server, set up DH
        logger->debug("setting up DH");
        if (!isconnect) {
                if (!SSLCALL(SSL_CTX_set_tmp_dh(ctx, ssl_setup_dh()))) {
                        THROW(ErrSSL, "SSL_CTX_set_tmp_dh()");
                }
        }

        // set up CRL check.
        // DISABLED: while this works, it gives the error message
        // SSL_accept()[...]no certificate returned.
        // so check is made after connection is made, at the end of this
        // function
        if (0 && !crlfile.empty()) {
                // http://bugs.unrealircd.org/view.php?id=2043
                X509_STORE *store = SSLCALL(SSL_CTX_get_cert_store(ctx));
                X509_LOOKUP *lookup;
                lookup = SSLCALL(X509_STORE_add_lookup(store,
                                                       X509_LOOKUP_file()));

                if (!SSLCALL(X509_load_crl_file(lookup,
                                                crlfile.c_str(),
                                                X509_FILETYPE_PEM))) {
                        THROW(ErrSSL, "X509_load_crl_file()");
                }

                if (!SSLCALL(X509_STORE_set_flags(store,
                                                  X509_V_FLAG_CRL_CHECK
                                                  | X509_V_FLAG_CRL_CHECK_ALL
                                                  ))) {
                        THROW(ErrSSL, "X509_STORE_set_flags()");
                }
        }

        // create ssl object
        if (!(ssl = SSLCALL(SSL_new(ctx)))) {
                THROW(ErrSSL, "SSL_new()");
	}

        // attach fd to ssl object
        if (!SSLCALL(SSL_set_fd(ssl, fd.get()))) {
                THROW(ErrSSL, "SSL_set_fd()", ssl, err);
        }

        // do handshake
        logger->debug("doing SSL handshake");
	if (isconnect) {
                err = SSLCALL(SSL_connect(ssl));
		if (err == -1) {
                        THROW(ErrSSL, "SSL_connect()", ssl, err);
		}

                // FIXME: log actual verify error.
                if (SSLCALL(SSL_get_verify_result(ssl)) != X509_V_OK) {
                        THROW(ErrSSL, "SSL_get_verify_result() != X509_V_OK");
		}

                X509Wrap x(SSLCALL(SSL_get_peer_certificate(ssl)));
		if (!x.check_hostname(host)) {
                        THROW(ErrSSLHostname, host, x.get_subject());
		}
	} else {
                err = SSLCALL(SSL_accept(ssl));
		if (err == -1) {
                        THROW(ErrSSL, "SSL_accept()", ssl, err);
		}
	}

        // if debug, show cert info
	X509Wrap x(SSL_get_peer_certificate(ssl));
        logger->debug("Issuer: %s\nSubject: %s\n"
                      "Cipher: %s (Version %d bits) %s\n"
                      "Fingerprint: %s",
                      x.get_issuer().c_str(),
                      x.get_subject().c_str(),
                      SSLCALL(SSL_get_cipher_name(ssl)),
                      SSLCALL(SSL_get_cipher_bits(ssl, 0)),
                      SSLCALL(SSL_get_cipher_version(ssl)),
                      x.get_fingerprint().c_str());

        check_crl();
        if (isconnect) {
                check_ocsp();
        }
}

/**
 * http://etutorials.org/Programming/secure+programming/Chapter+10.+Public+Key+Infrastructure/10.12+Checking+Revocation+Status+via+OCSP+with+OpenSSL/
 *
 * @todo Implement OSCP
 */
void
SSLSocket::check_ocsp()
{
#if 0
        const char *url = "http://ocsp.cacert.org/";
        char *host = 0;
        char *port = 0;
        SSL_CTX               *ctx2 = 0;
        X509_STORE            *store = 0;
        OCSP_CERTID           *id;
        OCSP_REQUEST          *req = 0;
        OCSP_RESPONSE         *resp = 0;
        OCSP_BASICRESP        *basic = 0;
        ASN1_GENERALIZEDTIME  *producedAt, *thisUpdate, *nextUpdate;

        FINALLY(
#if 0
        // indent right in emacs
                );
#endif

        if (!OCSP_parse_url(url, &host, &port, &path, &ssl)) {
                THROW(ErrSSL, "OCSP_parse_url");
        }

        if (!(req = OCSP_REQUEST_new(  ))) {
                THROW(ErrSSL, "OCSP_REQUEST_new");
        }

        id = OCSP_cert_to_id(0, data->cert, data->issuer);
        if (!id || !OCSP_request_add0_id(req, id)) {
                THROW(ErrSSL, "OCSP_request_add0_id");
        }

        OCSP_request_add1_nonce(req, 0, -1);
        /* sign the request */
#if 0
        if (data->sign_cert && data->sign_key &&
            !OCSP_request_sign(req, data->sign_cert, data->sign_key, EVP_sha1(  ), 0, 0)) {
                THROW(ErrSSL, "OCSP_request_sign");
        }
#endif
        /* establish a connection to the OCSP responder */
        if (!(bio = spc_connect(host, atoi(port), ssl, data->store, &ctx))) {
                THROW(ErrSSL, "OSCP connect");
        }

        /* send the request and get a response */
        resp = OCSP_sendreq_bio(bio, path, req);
        if ((rc = OCSP_response_status(resp)) != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
                THROW(ErrSSL, "OCSP_response_status");
        }

        /* verify the response */
        if (!(basic = OCSP_response_get1_basic(resp))) {
                THROW(ErrSSL, "OCSP_response_get1_basic");
        }
        if (OCSP_check_nonce(req, basic) <= 0) {
                THROW(ErrSSL, "OCSP_check_nonce");
        }
        if (data->store && !(store = spc_create_x509store(data->store))) {
                THROW(ErrSSL, "spc_create_x509store");
        }
        if ((rc = OCSP_basic_verify(basic, 0, store, 0)) <= 0) {
                THROW(ErrSSL, "OCSP_basic_verify");
        }
        if (!OCSP_resp_find_status(basic, id, &status, &reason, &producedAt,
                                   &thisUpdate, &nextUpdate)) {
                THROW(ErrSSL, "OCSP_resp_find_status");
        }
        if (!OCSP_check_validity(thisUpdate,
                                 nextUpdate, data->skew, data->maxage)) {
                THROW(ErrSSL, "OCSP_check_validity");
        }
        /* All done.  Set the return code based on the status from the
           response. */
        if (status =  = V_OCSP_CERTSTATUS_REVOKED) {
                result = SPC_OCSPRESULT_CERTIFICATE_REVOKED;
        } else {
                result = SPC_OCSPRESULT_CERTIFICATE_VALID;
        }

        ,    /*  FINALLY */;

        if (bio) BIO_free_all(bio);
        if (host) OPENSSL_free(host);
        if (port) OPENSSL_free(port);
        if (path) OPENSSL_free(path);
        if (req) OCSP_REQUEST_free(req);
        if (resp) OCSP_RESPONSE_free(resp);
        if (basic) OCSP_BASICRESP_free(basic);
        if (ctx) SSL_CTX_free(ctx);
        if (store) X509_STORE_free(store);
#if 0
        // indent right in emacs
        (
#endif
        );
#endif
}

/**
 * check CRL.
 *
 * @todo CRL only works if cafile is used, not capath
 * http://etutorials.org/Programming/secure+programming/Chapter+10.+Public+Key+Infrastructure/10.5+Performing+X.509+Certificate+Verification+with+OpenSSL/
 */
void
SSLSocket::check_crl()
{
        int err;

        if (crlfile.empty()) {
                return;
        }

        X509_STORE_CTX *crlctx = 0;
        X509_STORE *store = 0;
        X509_LOOKUP *lookup = 0;
        X509Wrap cert(SSLCALL(SSL_get_peer_certificate(ssl)));

        FINALLY(
#if 0
                );        // indent right in emacs
#endif

        crlctx = SSLCALL(X509_STORE_CTX_new());
        if (!crlctx) {
                THROW(ErrSSL, "X509_STORE_CTX_new()");
        }

        store = SSLCALL(X509_STORE_new());
        if (!store) {
                THROW(ErrSSL, "X509_STORE_new()");
        }

        lookup = SSLCALL(X509_STORE_add_lookup(store,
                                               X509_LOOKUP_file()));

        if (!SSLCALL(X509_load_cert_file(lookup,
                                         cafile.c_str(),
                                         X509_FILETYPE_PEM))) {
                THROW(ErrSSL, "X509_load_cert_file()");
        }

        if (!SSLCALL(X509_load_crl_file(lookup,
                                        crlfile.c_str(),
                                        X509_FILETYPE_PEM))) {
                if (!SSLCALL(X509_load_crl_file(lookup,
                                                crlfile.c_str(),
                                                X509_FILETYPE_ASN1))) {
                        THROW(ErrSSL, "X509_load_crl_file()");
                }
        }

        SSLCALL(X509_STORE_set_flags(store,
                                     X509_V_FLAG_CRL_CHECK
                                     | X509_V_FLAG_CRL_CHECK_ALL));

        SSLCALL(X509_STORE_CTX_init(crlctx, store, cert.get(), 0));
        if (1 != SSLCALL(X509_verify_cert(crlctx))) {
                err = SSLCALL(X509_STORE_CTX_get_error(crlctx));
                THROW(ErrSSLCRL,
                      X509Wrap::errstr(err) + ": " + cert.get_subject());
        }

        ,    /*  FINALLY */;

        if (crlctx) { SSLCALL(X509_STORE_CTX_free(crlctx)); }
        if (store) { SSLCALL(X509_STORE_free(store)); } // need this? 
#if 0
        (        // indent right in emacs
#endif
         );
}

/**
 * write to socket. Not everthing may be written
 *
 * @param[in] buf  Data to write
 *
 * @return number of bytes actually written. Can be 0.
 */
size_t
SSLSocket::write(const std::string &buf)
{
        if (!ssl) {
                THROW(ErrSSL, "SSL write when not connected");
        }
        int ret;
        ret = SSLCALL(SSL_write(ssl, buf.data(), buf.length()));
        if (ret < 0) {
                THROW(ErrSSL, "SSL_write()", ssl,
                      SSLCALL(SSL_get_error(ssl, ret)));
        }
	return ret;
}

/**
 * read at most 'm' bytes of data
 *
 * @param[in] m Max bytes to read
 *
 * @return Data read
 *
 * Throws exception on error or EOF 
 */
std::string
SSLSocket::read(size_t m)
{
        if (!ssl) {
                THROW(ErrSSL, "SSL read when not connected");
        }
	int err, sslerr;
	std::vector<char> buf(m);
		
        err = SSLCALL(SSL_read(ssl, &buf[0], m));
	if (err > 0) {
		return std::string(&buf[0], &buf[err]);
	}
        sslerr = SSLCALL(SSL_get_error(ssl, err));
	if (err == 0 && sslerr == SSL_ERROR_ZERO_RETURN) {
                THROW0(ErrPeerClosed);
	}
        THROW(ErrSSL, "SSL_read()", ssl, err);
}
	
/**
 * @return true if there is any data waiting to be read
 */
bool
SSLSocket::ssl_pending()
{
        return SSLCALL(SSL_pending(ssl));
}

/**
 * Set list of ciphers that are acceptable. See ciphers(1SSL)
 */
void
SSLSocket::ssl_set_cipher_list(const std::string &lst)
{
	cipher_list = lst;
}

/**
 *
 */
void
SSLSocket::ssl_set_privkey_engine(const std::string &engine)
{
        privkey_engine_ = std::make_pair(true, engine);
}

/**
 *
 */
void
SSLSocket::ssl_set_privkey_password(const std::string &pass)
{
        privkey_password_ = std::make_pair(true, pass);
}

/**
 *
 */
void
SSLSocket::ssl_set_tpm_srk_password(const std::string &pass)
{
        tpm_srk_password_ = std::make_pair(true, pass);
}


/**
 * Set path where root CAs can be found.
 */
void
SSLSocket::ssl_set_capath(const std::string &s)
{
	capath = s;
}

/**
 * Set CRL file name
 */
void
SSLSocket::ssl_set_crlfile(const std::string &s)
{
	crlfile = s;
}

/**
 * set path to a root CA file
 */
void
SSLSocket::ssl_set_cafile(const std::string &s)
{
	cafile = s;
}

/**
 * Set path to .crt file (can be a combined crt/keyfile, but then
 * ssl_set_keyfile() must also be called)
 */
void
SSLSocket::ssl_set_certfile(const std::string &s)
{
	certfile = s;
}

/**
 * Set path to .key file (can be a combined crt/keyfile, but then
 * ssl_set_certfile() must also be called)
 */
void
SSLSocket::ssl_set_keyfile(const std::string &s)
{
	keyfile = s;
}

/**
 * Extract a full SSL error queue of data
 */
SSLSocket::ErrSSL::ErrSSL(const Err::ErrData &errdata,
                          const std::string &s,
                          SSL *ssl, int err)
        :ErrBase(errdata,s)
{
	if (ssl) {
                sslmsg = SSLSocket::ssl_errstr(SSLCALL(SSL_get_error(ssl,
                                                                     err)));
                msg = msg + ": " + sslmsg;
	}

	for (;;) {
		unsigned long err;
		const char *file;
		int line;
		const char *data;
		int flags;
                err = SSLCALL(ERR_get_error_line_data(&file,
                                                      &line,
                                                      &data,
                                                      &flags));
		if (!err) {
			break;
		}
		SSLSocket::ErrQueueEntry e;
		e.file = file;
		e.line = line;
		e.data = data;
		e.flags = flags;
		char buf[1024];
		ERR_error_string_n(err, buf, sizeof(buf));
		e.str = buf;
		errqueue.push_back(e);
	}
}

/**
 * Construct a string of human-readable messages from the error queue
 */
std::string
SSLSocket::ErrSSL::what_verbose() const throw()
{
	errqueue_t::const_iterator itr;
	std::stringstream ret;
	int c = 0;

	ret << "------- SSL Error -------" << std::endl
	    << "Exception message: " << what() << std::endl;

	for (itr = errqueue.begin();
	     itr != errqueue.end();
	     ++itr) {
		ret << "SSL Error number " << ++c << ":" << std::endl
		    << "  " << itr->str << std::endl
		    << "  File:  " << itr->file << std::endl
		    << "  Line:  " << itr->line << std::endl
		    << "  Data:  " << itr->data << std::endl
		    << "  Flags: " << itr->flags << std::endl
			;
	}
	return ret.str();
}

/**
 *
 */
SSLSocket::ErrSSLHostname::ErrSSLHostname(const Err::ErrData &errdata,
                                          const std::string &host,
					  const std::string &subject)
        :ErrSSL(errdata, "SSL Hostname does not match subject")
{
	msg = "Cert " + subject + " does not match hostname " + host;
}

/**
 *
 */
SSLSocket::ErrSSLCRL::ErrSSLCRL(const Err::ErrData &errdata,
                                const std::string &subject)
        :ErrSSL(errdata, "Cert revoked by CRL"), subject(subject)
{
        msg += ": " + subject;
}

/* ---- Emacs Variables ----
 * Local Variables:
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
