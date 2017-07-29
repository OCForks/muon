// chrome.cryptotokenPrivate.canOriginAssertAppId(origin, appId, resolve);

var binding = require('binding').Binding.create('cryptotokenPrivate')
var ipc = require('ipc_utils')
var lastError = require('lastError')

binding.registerCustomHook(function (bindingsAPI, extensionId) {
  var apiFunctions = bindingsAPI.apiFunctions

  apiFunctions.setHandleRequest('canOriginAssertAppId', function (origin, appId, cb) {
    // FIXME stubbed out
    cb(true)
  })
})

exports.$set('binding', binding.generate())
