/* Orchid - WebRTC P2P VPN Market (on Ethereum)
 * Copyright (C) 2017-2020  The Orchid Authors
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


#include <boost/beast/ssl/ssl_stream.hpp>

#include <boost/asio/ssl/rfc2818_verification.hpp>

#include "adapter.hpp"
#include "baton.hpp"
#include "ssl.hpp"

namespace orc {

task<Response> Fetch_(Adapter &socket, const http::request<http::string_body> &req, const std::string &host, const std::function<bool (const std::list<const rtc::OpenSSLCertificate> &)> &verify) { orc_ahead
    // XXX: this needs security
    asio::ssl::context context{asio::ssl::context::sslv23_client};

    if (!verify)
        context.set_verify_callback(asio::ssl::rfc2818_verification(host));
    else {
        context.set_verify_mode(asio::ssl::verify_peer);

        context.set_verify_callback([&](bool preverified, boost::asio::ssl::verify_context &context) {
            const auto store(context.native_handle());
            const auto chain(X509_STORE_CTX_get0_chain(store));
            std::list<const rtc::OpenSSLCertificate> certificates;
            for (auto e(sk_X509_num(chain)), i(decltype(e)(0)); i != e; i++)
                certificates.emplace_back(sk_X509_value(chain, i));
            return verify(certificates);
        });
    }

    boost::beast::ssl_stream<Adapter &> stream{socket, context};
    orc_assert(SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()));
    // XXX: beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};

    orc_block({ try {
        co_await stream.async_handshake(asio::ssl::stream_base::client, orc::Adapt());
    } catch (const asio::system_error &error) {
        orc_adapt(error);
    } }, "in ssl handshake");

    co_return co_await Fetch_(stream, req);
}

}
