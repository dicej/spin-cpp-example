#include <algorithm>
#include <span>
#include <vector>

#include "bindings/http_trigger_cpp.h"
#include "bindings/wit.h"

using wasi::http0_2_0::types::Fields;
using wasi::http0_2_0::types::IncomingBody;
using wasi::http0_2_0::types::IncomingRequest;
using wasi::http0_2_0::types::Method;
using wasi::http0_2_0::types::OutgoingBody;
using wasi::http0_2_0::types::OutgoingResponse;
using wasi::http0_2_0::types::ResponseOutparam;
using wasi::io0_2_0::streams::StreamError;

namespace {
std::span<const uint8_t> span(const char* string)
{
    return std::span(reinterpret_cast<const uint8_t*>(string), strlen(string));
}

void send_status(ResponseOutparam&& response_out, uint16_t status)
{
    auto response = OutgoingResponse(Fields());
    response.SetStatusCode(status);
    auto body = response.Body().value();

    ResponseOutparam::Set(std::move(response_out), std::move(response));

    OutgoingBody::Finish(std::move(body), std::nullopt);
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

        std::vector<uint8_t> content;
        {
            auto body = request.Consume().value();
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
                            send_status(std::move(response_out), 500);
                            return;
                        }
                    }
                }
            }
            IncomingBody::Finish(std::move(body));
        }

        OutgoingResponse response(Fields::FromList(headers).value());
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
    } else {
        send_status(std::move(response_out), 400);
    }
}
} // namespace exports::wasi::http0_2_0::incoming_handler
