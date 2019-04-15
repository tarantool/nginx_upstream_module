#!/usr/bin/python
# -_- encoding: utf8 -_-

import sys
import time
sys.path.append('./t')
from http_utils import *

please_unescape_it = BASE_URL + '/issue_120/'

print ('[+] unescape regression test')
args = {'arg1': 1, 'arg2': 'somestring'}
result = get_success(please_unescape_it + '19UM|SMSO/a%7Cx%3Db', args, {})
assert(result['uri'] == '/issue_120/19UM|SMSO/a|x=b?arg1=1&arg2=somestring'), \
    "expected result"
print ('[+] OK')

print ('[+] issue 120')
result = get_success(please_unescape_it + '%7B%7D', {}, {})
assert(result['uri'] == '/issue_120/{}'), "expected result"
print ('[+] OK')

