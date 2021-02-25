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


#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include "adapter.hpp"
#include "baton.hpp"
#include "fetch.hpp"
#include "http.hpp"
#include "locator.hpp"
#include "origin.hpp"
#include "ssl.hpp"

namespace orc {

task<Response> Fetch(Origin &origin, const std::string &method, const Locator &locator, const std::map<std::string, std::string> &headers, const std::string &data, const std::function<bool (const std::list<const rtc::OpenSSLCertificate> &)> &verify) { orc_ahead
    http::request<http::string_body> req{http::string_to_verb(method), locator.path_, 11};
    req.set(http::field::host, locator.host_);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    for (auto &[name, value] : headers)
        req.set(name, value);

    req.set(http::field::content_length, std::to_string(data.size()));
    req.body() = data;

    orc_block({
        const auto endpoints(co_await origin.Resolve(locator.host_, locator.port_));
        std::exception_ptr error;
        for (const auto &endpoint : endpoints) try {
            Adapter adapter(Context(), co_await origin.Connect(endpoint));
            orc_block({
                const auto response(co_await [&]() {
                    if (false);
                    else if (locator.scheme_ == "http")
                        return Fetch_(adapter, req);
                    else if (locator.scheme_ == "https")
                        return Fetch_(adapter, req, locator.host_, verify);
                    else orc_assert(false);
                }());
                // XXX: potentially allow this to be passed in as a custom response validator
                orc_assert_(response.result() != boost::beast::http::status::bad_gateway, response);
                co_return response;
            }, "connected to " << endpoint);
        } catch (...) {
            // XXX: maybe I should merge the exceptions? that would be cool
            if (error == nullptr)
                error = std::current_exception();
        }
        orc_assert_(error != nullptr, "failed connection");
        std::rethrow_exception(error);
    }, "requesting " << locator);
}

}
