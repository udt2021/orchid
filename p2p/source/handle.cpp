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


#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>

#include "baton.hpp"
#include "router.hpp"
#include "spawn.hpp"

namespace orc {

namespace beast = boost::beast;

template <bool Expires_, typename Stream_>
task<void> Router::Handle(Stream_ &stream, const Socket &socket) {
    beast::flat_buffer buffer;

    for (;;) {
        Request request(socket);
        try {
            co_await http::async_read(stream, buffer, request, Adapt());
        } catch (const asio::system_error &error) {
            const auto code(error.code());
            if (false);
            else if (code == asio::ssl::error::stream_truncated);
            else if (code == beast::error::timeout);
            else if (code == http::error::end_of_stream);
            else if (code == http::error::partial_message);
            else orc_adapt(error);
            co_return;
        }

        if (beast::websocket::is_upgrade(request)) {
            if constexpr (Expires_)
                beast::get_lowest_layer(stream).expires_never();
            Log() << request << std::endl;
            beast::websocket::stream<Stream_> ws(std::move(stream));
            ws.set_option(beast::websocket::stream_base::timeout::suggested(beast::role_type::server));
            ws.set_option(beast::websocket::stream_base::decorator([](beast::websocket::response_type &response) {
                response.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            }));
            co_await ws.async_accept(request, Adapt());

            for (;;) {
                const auto writ(co_await ws.async_read(buffer, Adapt()));
                orc_insist(buffer.size() == writ);
                Log() << writ << std::endl;
                ws.text(ws.got_text());
                orc_insist(co_await ws.async_write(buffer.data(), Adapt()) == writ);
                buffer.consume(writ);
            }

            co_return;
        }

        const auto response(co_await [&]() -> task<Response> { try {
            for (const auto &route : routes_)
                if (auto response = route(request))
                    co_return co_await std::move(response);
            Log() << request << std::endl;
            // XXX: maybe return method_not_allowed if path is found but method is not
            co_return Respond(request, http::status::not_found, "text/plain", "");
        } catch (const std::exception &error) {
            co_return Respond(request, http::status::internal_server_error, "text/plain", error.what());
        } }());

        co_await http::async_write(stream, response, Adapt());
        if (!response.keep_alive())
            co_return;
    }
}

}
