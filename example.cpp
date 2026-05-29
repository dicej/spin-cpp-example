#include <algorithm>
#include <span>
#include <vector>

#include "bindings/http_trigger_cpp.h"
#include "bindings/wit.h"
#include "deps/ada.cpp"
#include "deps/ada.h"

using namespace wasi::http0_2_0;
using wasi::http0_2_0::types::Fields;
using wasi::http0_2_0::types::IncomingBody;
using wasi::http0_2_0::types::IncomingRequest;
using wasi::http0_2_0::types::Method;
using wasi::http0_2_0::types::OutgoingBody;
using wasi::http0_2_0::types::OutgoingRequest;
using wasi::http0_2_0::types::OutgoingResponse;
using wasi::http0_2_0::types::ResponseOutparam;
using wasi::http0_2_0::types::Scheme;
using wasi::io0_2_0::streams::StreamError;

namespace {
    
std::span<const uint8_t> span(const char* string)
{
    return std::span(reinterpret_cast<const uint8_t*>(string), strlen(string));
}

std::expected<std::vector<uint8_t>, wit::Void> consume(IncomingBody&& body)
{
    std::vector<uint8_t> content;
    {
        auto stream = body.Stream().value();
        while (true) {
            auto result = stream.BlockingRead(64 * 1024);
            if (result.has_value()) {
                auto chunk = std::move(*result);
                content.insert(content.end(), chunk.data(), chunk.data() + chunk.size());
            } else {
                auto error = std::move(result.error());
                if (std::holds_alternative<StreamError::Closed>(error.variants)) {
                    break;
                } else {
                    return std::unexpected(wit::Void());
                }
            }
        }
    }
    IncomingBody::Finish(std::move(body));
    return content;
}

void send_status(ResponseOutparam&& response_out, uint16_t status)
{
    auto response = OutgoingResponse(Fields());
    response.SetStatusCode(status);
    auto body = response.Body().value();

    ResponseOutparam::Set(std::move(response_out), std::move(response));

    OutgoingBody::Finish(std::move(body), std::nullopt);
}

void send_content(ResponseOutparam&& response_out, OutgoingResponse&& response, std::vector<uint8_t>&& content)
{
    auto body = response.Body().value();

    ResponseOutparam::Set(std::move(response_out), std::move(response));

    {
        auto stream = body.Write().value();
        unsigned long offset = 0;
        unsigned long max = 4096; // Maximum `OutputStream::BlockingWriteAndFlush` buffer size
        while (offset < content.size()) {
            auto count = std::min(content.size() - offset, max);
            stream.BlockingWriteAndFlush(std::span(content).subspan(offset, count));
            offset += count;
        }
    }

    OutgoingBody::Finish(std::move(body), std::nullopt);
}

std::vector<std::tuple<std::string_view, std::span<const uint8_t>>> header_views(const wit::vector<std::tuple<wit::string, wit::vector<uint8_t>>>& headers)
{
    std::vector<std::tuple<std::string_view, std::span<const uint8_t>>> views;
    for (auto& pair : headers.get_view()) {
        views.push_back(std::tuple(std::get<0>(pair).get_view(), std::get<1>(pair).get_view()));
    }
    return views;
}

} // namespace

namespace exports::wasi::http0_2_0::incoming_handler {
    
void Handle(IncomingRequest&& request, ResponseOutparam&& response_out)
{
    auto method = request.Method();
    std::string path;
    auto path_with_query = request.PathWithQuery();
    if (path_with_query.has_value()) {
        path = path_with_query.value().to_string();
    } else {
        path = "/";
    }

    std::string proxy_prefix = "/proxy?";

    if (std::holds_alternative<Method::Get>(method.variants) && path == "/hello") {
        // Send a "Hello, world!" response

        std::tuple<std::string_view, std::span<const uint8_t>> headers[] {
            std::tuple("content-type", span("text/plain"))
        };

        OutgoingResponse response(Fields::FromList(headers).value());
        auto body = response.Body().value();

        ResponseOutparam::Set(std::move(response_out), std::move(response));

        body.Write().value().BlockingWriteAndFlush(span("Hello, world!\n"));
        OutgoingBody::Finish(std::move(body), std::nullopt);

    } else if (std::holds_alternative<Method::Get>(method.variants) && path.starts_with(proxy_prefix)) {
        // Extract a URL from the query string, send a request to that URL, and
        // forward the response back to the client.
        //
        // Here we buffer the entire body in memory before sending the response,
        // but we could alternatively stream the response in full-duplex mode,
        // if desired.

        ada::url_search_params query(std::string_view(path).substr(proxy_prefix.length()));
        auto url_string = query.get("url");
        if (url_string.has_value()) {
            auto url_result = ada::parse(url_string.value());
            if (url_result.has_value()) {
                auto url = std::move(url_result.value());
                auto protocol = url.get_protocol();
                auto outgoing = OutgoingRequest(Fields());
                Scheme scheme;
                if (protocol == "http") {
                    scheme.variants = Scheme::Http();
                } else if (protocol == "https") {
                    scheme.variants = Scheme::Https();
                } else {
                    scheme.variants = Scheme::Other { wit::string::from_view(protocol) };
                }
                outgoing.SetScheme(scheme);
                outgoing.SetAuthority(url.get_host());
                outgoing.SetPathWithQuery(url.get_pathname());

                auto future_result = outgoing_handler::Handle(std::move(outgoing), std::nullopt);
                if (future_result.has_value()) {
                    auto future = std::move(future_result.value());
                    while (true) {
                        auto incoming_result_option = future.Get();
                        if (incoming_result_option.has_value()) {
                            auto incoming_result = std::move(incoming_result_option.value().value());
                            if (incoming_result.has_value()) {
                                auto incoming = std::move(incoming_result.value());
                                auto status = incoming.Status();
                                auto headers = incoming.Headers().Entries();
                                auto content = consume(incoming.Consume().value());

                                if (content.has_value()) {
                                    OutgoingResponse response(Fields::FromList(header_views(headers)).value());
                                    response.SetStatusCode(status);
                                    send_content(
                                        std::move(response_out),
                                        std::move(response),
                                        std::move(content.value()));
                                } else {
                                    send_status(std::move(response_out), 500);
                                }
                            } else {
                                send_status(std::move(response_out), 500);
                            }
                            break;
                        } else {
                            future.Subscribe().Block();
                        }
                    }
                } else {
                    send_status(std::move(response_out), 500);
                }
            } else {
                send_status(std::move(response_out), 400);
            }
        } else {
            send_status(std::move(response_out), 400);
        }

    } else if (std::holds_alternative<Method::Post>(method.variants) && path == "/echo") {
        // Accept a POST'ed body and return a response echoing that body.
        //
        // Here we buffer the entire body in memory before sending the response,
        // but we could alternatively stream the response in full-duplex mode,
        // if desired.

        wit::vector<uint8_t> content_type;
        {
            auto headers = request.Headers().Entries();
            for (auto& pair : headers.get_view()) {
                if (std::get<0>(pair).to_string() == "content-type") {
                    content_type = std::move(std::get<1>(pair));
                }
            }
        }

        std::vector<std::tuple<std::string_view, std::span<const uint8_t>>> headers;
        if (!content_type.empty()) {
            headers.push_back(std::tuple(std::string_view("content-type"), content_type.get_view()));
        }

        auto content = consume(request.Consume().value());

        if (content.has_value()) {
            OutgoingResponse response(Fields::FromList(headers).value());
            send_content(std::move(response_out), std::move(response), std::move(content.value()));
        } else {
            send_status(std::move(response_out), 500);
        }
    } else {
        send_status(std::move(response_out), 400);
    }
}

} // namespace exports::wasi::http0_2_0::incoming_handler
