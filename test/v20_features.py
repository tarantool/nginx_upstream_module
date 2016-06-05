#!/usr/bin/python

import json
import urllib
import urllib2
import traceback
import sys

VERBOSE = False
BASE_URL = 'http://127.0.0.1:8081'

def post(url, data, headers):
    out = '{}'
    try:
        req = urllib2.Request(url)
        if headers:
            for header in headers:
                req.add_header(header, headers[header])
        req.add_header('Content-Type', 'application/json')

        res = urllib2.urlopen(req, json.dumps(data))
        out = res.read()
        out = out + res.read()
        rc = res.getcode()

        if VERBOSE:
            print "code: ", rc, " recv: '", out, "'"

        if rc != 500:
            return (rc, json.loads(out))

        return (rc, False)
    except urllib2.HTTPError, e:
        if e.code == 400:
            out = e.read();

        if VERBOSE:
            print "code: ", e.code, " recv: '", out, "'"

        return (e.code, json.loads(out))
    except Exception, e:
        print traceback.format_exc()
        return (False, e)

def put(url, data, headers):
    out = '{}'
    try:
        req = urllib2.Request(url)
        req.get_method = lambda: 'PUT'
        if headers:
            for header in headers:
                req.add_header(header, headers[header])
        if data:
            req.add_header('Content-Type', 'application/json')
            res = urllib2.urlopen(req, json.dumps(data))
        else:
            res = urllib2.urlopen(req)

        out = res.read()
        out = out + res.read()
        rc = res.getcode()

        if VERBOSE:
            print "code: ", rc, " recv: '", out, "'"

        if rc != 500:
            return (rc, json.loads(out))

        return (rc, False)
    except urllib2.HTTPError, e:
        if e.code == 400:
            out = e.read();

        if VERBOSE:
            print "code: ", e.code, " recv: '", out, "'"

        return (e.code, json.loads(out))
    except Exception, e:
        print traceback.format_exc()
        return (False, e)

def get(url, data, headers):
    out = '{}'
    try:
        full_url = url
        if data:
            full_url = url + '?' + urllib.urlencode(data)
        req = urllib2.Request(full_url)
        if headers:
            for header in headers:
                req.add_header(header, headers[header])

        res = urllib2.urlopen(req)
        out = res.read()
        out = out + res.read()
        rc = res.getcode()

        if VERBOSE:
            print "code: ", rc, " recv: '", out, "'"

        if rc != 500:
            return (rc, json.loads(out))

        return (rc, False)
    except urllib2.HTTPError, e:
        if e.code == 400:
            out = e.read();

        if VERBOSE:
            print "code: ", e.code, " recv: '", out, "'"

        return (e.code, json.loads(out))
    except Exception, e:
        print traceback.format_exc()
        return (False, e)

def get_result(o):
    assert('result' in o), "expected 'result'"
    return o['result'][0]

def assert_if_not_error(s, code = None):
    assert('error' in s), 'expected error'
    assert('message' in s['error']), 'expected error message'
    assert('code' in s['error']), 'expected error message'
    if code:
        assert(s['error']['code'] == code), 'expected code'

def get_success(url, data, headers):
    (code, msg) = get(url, data, headers)
    assert(code == 200), 'expected 200'
    result = get_result(msg)
    return result

def get_success_pure(url, data, headers):
    (code, msg) = get(url, data, headers)
    assert(code == 200), 'expected 200'
    return msg

def post_success(url, data, headers):
    (code, msg) = post(url, data, headers)
    assert(code == 200), 'expected 200'
    result = get_result(msg)
    return result

def post_success_pure(url, data, headers):
    (code, msg) = post(url, data, headers)
    assert(code == 200), 'expected 200'
    return msg

def put_success(url, data, headers):
    (code, msg) = put(url, data, headers)
    assert(code == 200), 'expected 200'
    result = get_result(msg)
    return result

def get_api_fail(url, data, headers):
    (code, msg) = get(url, data, headers)
    return msg

def assert_headers(result, headers_in):
    for header in headers_in:
        header_from_server = result[0]['headers'][header]
        assert(header_from_server == headers_in[header]), 'expected headers_in'

def assert_headers_pure(result, headers_in):
    for header in headers_in:
        header_from_server = result['headers'][header]
        assert(header_from_server == headers_in[header]), 'expected headers_in'

big_args_in = {}
big_headers_in = {}
headers_in = {'My-Header': '1', 'My-Second-Header': '2'}
for i in range(1, 20):
    i_str = str(i)
    big_headers_in['My-Header-' + i_str] = i_str
    big_args_in['arg' + i_str] = i

# =============
#
print('[+] Rest interface test')

preset_method_location = BASE_URL + '/preset_method/should_not_call_this'
result = get_success(preset_method_location,
                     {'arg1': 1, 'arg2': 2},
                     headers_in)
assert_headers(result, headers_in)

rest_location = BASE_URL + '/echo_2'
result = get_success(rest_location,
                     {'arg1': 1, 'arg2': 2},
                     headers_in)
for header in headers_in:
    header_from_server = result[0]['headers'][header]
    assert(header_from_server == headers_in[header]), 'expected headers_in'

result = get_success(rest_location + '/method_does_not_exists',
                      {'arg1': 1, 'arg2': 2},
                       headers_in)
assert_headers(result, headers_in)

overflow_rest_api_location = BASE_URL + '/echo_2/overflow_rest_api'
(code, result) = get(overflow_rest_api_location, big_args_in, big_headers_in)
assert(code == 500), 'expected 500'

# =============
#
print('[+] New Put/Delete features')

preset_method_location = BASE_URL + '/preset_method'
result = put_success(preset_method_location,
                     {'params': [{"arg1": 1}], 'id': 1},
                     None)
assert(result[0]["arg1"] == 1), "expected arg1 == 1"

rest_api_location = BASE_URL + '/echo_2'
result = put_success(rest_api_location,
                     {'params': [{"arg1": 1}], 'id': 1},
                     headers_in)
assert_headers(result, headers_in)
assert(result[1]['arg1'] == 1), "expected arg1 == 1"

# =============
#
print('[+] New post features test')

post_pass_http_request_location = BASE_URL + '/post_pass_http_request'
result = post_success(post_pass_http_request_location,
                      {'method': 'echo_2', 'params': [], 'id': 1},
                      headers_in)
assert_headers(result, headers_in)


post_pass_preset_method_location = BASE_URL + '/post_preset_method'
result = post_success(post_pass_preset_method_location,
                      {'params': [], 'id': 1},
                      headers_in)
assert_headers(result, headers_in)

post_pass_http_request_location = BASE_URL + '/post_preset_method'
result = post_success(post_pass_http_request_location,
                      {'method': 'method_does_not_exists', 'params': [], 'id': 1},
                      headers_in)
assert_headers(result, headers_in)

overflow_post_api_location = BASE_URL + '/overflow_post_pass_http_request'
(code, result) = post(overflow_post_api_location, big_args_in, big_headers_in)
assert(code == 500), 'expected 500'

# ============
#
print('[+] Pure & skip count test')

result = get_success_pure(BASE_URL + "/pure_result_rest",
                          {'arg1': 1, 'arg2': 2},
                          headers_in)
assert_headers(result[0], headers_in)

result = get_success_pure(BASE_URL + "/pure_result_rest_skip_count_2",
                          {'arg1': 1, 'arg2': 2},
                          headers_in)
assert_headers_pure(result, headers_in)

result = post_success_pure(BASE_URL + "/post_pure_result",
                          {'method': 'echo_2', 'params': [1, 2], 'id': 1},
                          headers_in)
assert(result[0][0] == 1), "expected [[1,..]]"
assert(result[0][1] == 2), "expected [[..,2]]"

result = post_success_pure(BASE_URL + "/post_pure_result_skip_count_1",
                          {'method': 'echo_2', 'params': [1, 2], 'id': 1},
                          headers_in)
assert(result[0] == 1), "expected [1,..]"
assert(result[1] == 2), "expected [..,2]"

# ============
# Issue 37 test
print('[+] Issue: https://github.com/tarantool/nginx_upstream_module/issues/37')

expected = {"status":{
    "code":200,
    "text":"OK"},
    "meta":{"debug":{"front":"app-2","auth":True,"source":False},
    "exec_time":0.005933,
    "related":["/ucp/0001/accounts/account/79031234567/services"]},
    "data":{"p2":79031234568,"p1":79031234567,"account":79031234567},
    "aux":{"hello":"hello 79031234567"}}

result = get_success_pure(BASE_URL + "/ucp/read", {}, {})
diff = cmp(result, expected)
assert(diff == 0), '(GET) Issue 37 - have diff: ' + str(diff)

result = post_success_pure(BASE_URL + "/ucp",
                          {'method': 'ucp', 'params': [], 'id': 1}, {})
diff = cmp(result, expected)
assert(diff == 0), '(POST) Issue 37 - have diff: ' + str(diff)
