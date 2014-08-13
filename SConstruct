# Set up the default build environment
env = Environment()
Export('env')

# Read build config settings from build.conf and arguments
vars = Variables('build.conf', ARGUMENTS)
vars.Add(EnumVariable('mode', 'Build in debug or release mode?', 'debug',
                      allowed_values=('debug', 'release')))
vars.Add('prefix', 'Installation prefix', '/usr/local')

# Add variable settings to the environment
vars.Update(env)

# Save arguments (and defaults)
vars.Save('build.conf', env)

if env['mode'] == 'debug':
  build_dir = '.debug'
  env.Append(CCFLAGS='-g -O2')
  
else:
  build_dir = '.release'
  env.Append(CCFLAGS='-DNDEBUG -O3')

# Build with clang
env.Replace(CC='clang')
env.Replace(CXX='clang++')

# Set C++11 mode, generate position-independent code
env.Append(CCFLAGS='--std=c++11 -fPIC')

# Add include paths
env.Append(CPPPATH=['include', 
                    '#/deps/libelfin/dwarf',
                    '#/deps/libelfin/elf',
                    '#/deps/cppgoodies/include'])

# Add lib paths
env.Append(LIBPATH=['#/' + build_dir + '/deps/libelfin/dwarf',
                    '#/' + build_dir + '/deps/libelfin/elf',
                    '#/' + build_dir + '/lib/support'])

# Go
env.SConscript('SConscript', variant_dir=build_dir, duplicate=0)

# Default to 'all' build target
env.Default('all')