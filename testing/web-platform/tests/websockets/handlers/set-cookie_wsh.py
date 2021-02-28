#!/usr/bin/python
import urlparse


def web_socket_do_extra_handshake(request):
    url_parts = urlparse.urlsplit(request.uri)
    request.extra_headers.append(('Set-Cookie', 'ws_test_'+(url_parts.query or '')+'=test; Path=/'))

def web_socket_transfer_data(request):
    # Expect close from user agent.
    request.ws_stream.receive_message()