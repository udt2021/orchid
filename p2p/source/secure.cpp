/* Orchid - WebRTC P2P VPN Market (on Ethereum)
 * Copyright (C) 2017-2019  The Orchid Authors
*/

/* GNU Affero General Public License, Version 3 {{{ */
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.

 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/
/* }}} */


#include "rtc_base/openssl_identity.h"

#include "secure.hpp"
#include "scope.hpp"

namespace orc {

[[noreturn]]
static void Throw() {
    for (;;) {
        auto error(ERR_get_error());
        if (error == 0)
            break;
        Log() << "\e[31;1m" << ERR_reason_error_string(error) << " (e=" << error << ")" << "\e[0m" << std::endl;
    }

    _assert(false);
}

template <typename Code_>
static int Call(SSL *ssl, bool write, Code_ code) {
    _assert(ERR_get_error() == 0);
    auto value(std::move(code)());
    switch (auto error = SSL_get_error(ssl, value)) {
        case SSL_ERROR_NONE:
            return value;

        case SSL_ERROR_SSL:
        case SSL_ERROR_SYSCALL:
            Throw();

        case SSL_ERROR_WANT_READ:
            _assert(!write);
            return -1;

        case SSL_ERROR_WANT_WRITE:
            _assert(write);
            return -1;

        case SSL_ERROR_ZERO_RETURN:
            // XXX: should this do anything special?
        default:
            Log()("SSL_get_error(%x) = %x\n", value, error);
            _assert(false);
    }
}

Secure *Secure::Get(BIO *bio) {
    return static_cast<Secure *>(BIO_get_data(bio));
}

BIO_METHOD *Secure::Method() {
    static auto method([]() {
        auto method(BIO_meth_new(BIO_TYPE_BIO, "orchid"));

        BIO_meth_set_write(method, [](BIO *bio, const char *data, int size) -> int {
            return Get(bio)->Write(bio, data, size);
        });

        BIO_meth_set_read(method, [](BIO *bio, char *data, int size) -> int {
            return Get(bio)->Read(bio, data, size);
        });

        BIO_meth_set_puts(method, [](BIO *bio, const char *data) -> int {
            return Get(bio)->Write(bio, data, strlen(data));
        });

        BIO_meth_set_ctrl(method, [](BIO *bio, int command, long arg1, void *arg2) -> long {
            return Get(bio)->Control(bio, command, arg1, arg2);
        });

        BIO_meth_set_create(method, [](BIO *bio) -> int {
            BIO_set_shutdown(bio, 0);
            BIO_set_data(bio, 0);
            BIO_set_init(bio, 1);
            return 1;
        });

        BIO_meth_set_destroy(method, [](BIO *bio) -> int {
            if (bio == NULL)
                return 0;
            return Get(bio)->Destroy(bio);
        });

        return method;
    }());

    return method;
}

int Secure::Write(BIO *bio, const char *data, int size) {
    // XXX: implement a true non-blocking zero-copy write
    Task([this, beam = Beam(data, size)]() -> task<void> {
        co_await sink_->Send(beam);
    });
    return size;
}

int Secure::Read(BIO *bio, char *data, int size) {
    if (eof_) {
        _assert(data_ == NULL);
        return 0;
    } else if (data_ == NULL) {
        BIO_set_retry_read(bio);
        return -1;
    } else {
        auto writ(data_->copy(data, size));
        data_ = NULL;
        return writ;
    }
}

long Secure::Control(BIO *bio, int command, long arg1, void *arg2) {
    switch (command) {
        case BIO_CTRL_RESET:
            return -1;
        case BIO_CTRL_EOF:
            return eof_ ? 1 : 0;
        case BIO_CTRL_WPENDING:
        case BIO_CTRL_PENDING:
            return 0;
        case BIO_CTRL_FLUSH:
            return 1;
        case BIO_CTRL_DGRAM_QUERY_MTU:
            // XXX: implement MTU discovery
            return 1200;
        default:
            // XXX: this is what webrtc does
            return 0;
    }
}

int Secure::Destroy(BIO *bio) {
    return 1;
}

void Secure::Active() {
    for (;;) {
        uint8_t data[2048];

        int size;
        try {
            size = Call(ssl_, false, [&]() {
                return SSL_read(ssl_, data, sizeof(data));
            });
        } catch (const Error &error) {
            next_ = NULL;
            auto message(error.message);
            _assert(!message.empty());
            Link::Stop(message);
            break;
        }

        if (size == -1)
            break;
        else if (size == 0) {
            next_ = NULL;
            Link::Stop();
            break;
        }

        if (Verbose)
            Log() << "\e[33;1mRECV " << size << " " << Subset(data, size) << "\e[0m" << std::endl;

        Link::Land(Subset(data, size));
    }
}

void Secure::Server() {
    if (Call(ssl_, false, [&]() {
        return SSL_accept(ssl_);
    }) != -1) {
        next_ = &Secure::Active;
        opened_.set();
        (this->*next_)();
    }
}

void Secure::Client() {
    if (Call(ssl_, false, [&]() {
        return SSL_connect(ssl_);
    }) != -1) {
        next_ = &Secure::Active;
        opened_.set();
        (this->*next_)();
    }
}

void Secure::Land(const Buffer &data) {
    _assert(data_ == NULL);
    data_ = &data;
    _assert(next_ != NULL);
    (this->*next_)();
    _assert(data_ == NULL);
}

void Secure::Stop(const std::string &error) {
    _assert(data_ == NULL);
    eof_ = true;
    _assert(next_ != NULL);
    (this->*next_)();
}

Secure::Secure(bool server, U<Link> link, decltype(verify_) verify) :
    server_(server),
    sink_(this, std::move(link)),
    verify_(std::move(verify))
{
    auto context(SSL_CTX_new(server_ ? DTLS_method() : DTLS_client_method()));
    _scope({ SSL_CTX_free(context); });

    SSL_CTX_set_info_callback(context, [](const SSL *ssl, int where, int value) {
        auto self(static_cast<Secure *>(SSL_get_app_data(ssl)));
        if (Verbose || true)
            Log() << "\e[32;1mSSL :: " << self << " " << SSL_state_string_long(ssl) << "\e[0m" << std::endl;
    });

    SSL_CTX_set_min_proto_version(context, DTLS1_VERSION);
    SSL_CTX_set_cipher_list(context, "DEFAULT:!NULL:!aNULL:!SHA256:!SHA384:!aECDH:!AESGCM+AES256:!aPSK");

    {
        long one_day(60 * 60 * 24);
        time_t now(time(nullptr));
        rtc::SSLIdentityParams params;
        params.key_params = rtc::KeyParams(rtc::KT_DEFAULT);
        params.common_name = "client";
        params.not_before = now - one_day;
        params.not_after = now + one_day;
        auto identity(rtc::OpenSSLIdentity::GenerateForTest(params));
        identity->ConfigureIdentity(context);
    }

    SSL_CTX_set_verify(context, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    SSL_CTX_set_cert_verify_callback(context, [](X509_STORE_CTX *store, void *arg) -> int {
        return static_cast<Secure *>(arg)->verify_() ? 1 : 0;
    }, this);

    ssl_ = SSL_new(context);
    SSL_set_app_data(ssl_, this);

    //DTLSv1_set_initial_timeout_duration(ssl_, ...);

    auto bio(BIO_new(Method()));
    BIO_set_data(bio, this);
    SSL_set_bio(ssl_, bio, bio);

    if (server_) {
        next_ = &Secure::Server;
        Post([&]() {
            (this->*next_)();
        });
    }
}

task<void> Secure::_() {
    if (!server_) {
        next_ = &Secure::Client;
        Post([&]() {
            (this->*next_)();
        });
    }

    co_await opened_;
    co_await Schedule();
}

Secure::~Secure() {
_trace();
    SSL_free(ssl_);
}

task<void> Secure::Send(const Buffer &data) {
    if (Verbose)
        Log() << "\e[35;1mSEND " << data.size() << " " << data << "\e[0m" << std::endl;
    _assert(opened_.is_set());
    Beam beam(data);
    auto lock(co_await send_.scoped_lock_async());
    Post([&]() {
        _assert(Call(ssl_, true, [&]() {
            return SSL_write(ssl_, beam.data(), beam.size());
        }) != -1);
    });
}

task<void> Secure::Shut() {
    // XXX: implement
    co_await Link::Shut();
}

}
