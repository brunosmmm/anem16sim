project('anem16sim', 'cpp',
  version : '1.0',
  default_options : ['cpp_std=c++20', 'warning_level=2'])

# Dependencies
libzmq_dep = dependency('libzmq')
cppzmq_dep = dependency('cppzmq', fallback : ['cppzmq', 'cppzmq_dep'])
nlohmann_json_dep = dependency('nlohmann_json', fallback : ['nlohmann_json', 'nlohmann_json_dep'])
libwebsockets_dep = dependency('libwebsockets')

sources = [
  'alu.cpp',
  'cpu.cpp',
  'debug.cpp',
  'debug_engine.cpp',
  'debug_server.cpp',
  'disasm.cpp',
  'except.cpp',
  'gdb_stub.cpp',
  'instrset.cpp',
  'main.cpp',
  'mem.cpp',
  'regbnk.cpp',
  'snapshot.cpp',
  'trace.cpp',
  'web_server.cpp',
  'periph/mac.cpp',
  'periph/gpio.cpp',
  'periph/timer.cpp',
  'periph/uart.cpp',
  'stats/stats.cpp',
]

executable('sim', sources,
  dependencies : [libzmq_dep, cppzmq_dep, nlohmann_json_dep, libwebsockets_dep])
