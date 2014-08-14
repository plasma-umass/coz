import sys

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
env.Append(CPPPATH=['include', '#/deps/cppgoodies/include'])

# Add lib paths
env.Append(LIBPATH=['#/' + build_dir + '/lib/support'])

# Set up concise build output
env['CCCOMSTR'] = 'Compiling $SOURCE'
env['SHCCCOMSTR'] = 'Compiling $SOURCE'
env['CXXCOMSTR'] = 'Compiling $SOURCE'
env['SHCXXCOMSTR'] = 'Compiling $SOURCE'
env['LINKCOMSTR'] = 'Linking $TARGET'
env['SHLINKCOMSTR'] = 'Linking $TARGET'
env['ARCOMSTR'] = 'Linking $TARGET'
env['RANLIBCOMSTR'] = 'Indexing $TARGET'

# Go
env.SConscript('SConscript', variant_dir=build_dir, duplicate=0)

# Default to 'all' build target
env.Default('all')