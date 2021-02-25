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


#include <boost/beast/http.hpp>

#include "baton.hpp"
#include "http.hpp"

namespace orc {

template <typename Stream_>
task<Response> Fetch_(Stream_ &stream, const http::request<http::string_body> &req) { orc_ahead
    orc_block({ (void) co_await http::async_write(stream, req, orc::Adapt()); },
        "writing http request");

    // this buffer must be maintained if this socket object is ever reused
    boost::beast::flat_buffer buffer;
    http::response<http::dynamic_body> res;
    orc_block({ (void) co_await http::async_read(stream, buffer, res, orc::Adapt()); },
        "reading http response");

    // XXX: I can probably return this as a buffer array
    Response response(res.result(), req.version());;
    response.body() = boost::beast::buffers_to_string(res.body().data());
    co_return response;
}

}
