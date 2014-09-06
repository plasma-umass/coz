import os
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
  build_dir = 'debug'
  env.Append(CCFLAGS='-g -O2')
  
else:
  build_dir = 'release'
  env.Append(CCFLAGS='-DNDEBUG -O3')

# Build with clang
env.Replace(CC='clang')
env.Replace(CXX='clang++')

# Set C++11 mode, generate position-independent code
env.Append(CCFLAGS='--std=c++11 -fPIC')

# Add include paths
env.Append(CPPPATH=['#/include'])

# Add lib paths
env.Append(LIBPATH=['#/' + build_dir + '/lib/support'])

# Set up concise build output
env['CCCOMSTR'] = 'Compiling $SOURCE'
env['SHCCCOMSTR'] = 'Compiling $SOURCE'
env['CXXCOMSTR'] = 'Compiling $SOURCE'
env['SHCXXCOMSTR'] = 'Compiling $SOURCE'
#env['LINKCOMSTR'] = 'Linking $TARGET'
#env['SHLINKCOMSTR'] = 'Linking $TARGET'
env['ARCOMSTR'] = 'Linking $TARGET'
env['RANLIBCOMSTR'] = 'Indexing $TARGET'

if not os.path.isdir('include/ccutil'):
  print >> sys.stdout, 'Checking out github.com/ccurtsinger/ccutil'
  os.system('git clone --quiet git://github.com/ccurtsinger/ccutil include/ccutil > /dev/null')

ccutil_update = env.Command('#/include/ccutil/.git',
                            [], 
                            Action('cd include/ccutil; git pull > /dev/null', 
                                   'Updating include/ccutil'))
env.Depends(Glob('#/include/ccutil/*.h'), ccutil_update)
env.AlwaysBuild(ccutil_update)

# Go
env.SConscript('SConscript', variant_dir=build_dir, duplicate=0)

# Default to 'all' build target
env.Default('all')