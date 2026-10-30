cd /tmp
pwd
cd /
pwd
cd -
export FOO=bar
echo $FOO
sh -c 'echo child-sees=$FOO'
unset FOO
echo "[$FOO]"
BAR=temp sh -c 'echo inline=$BAR'
echo "[$BAR]"
cd /nonexistent_xyz 2>/dev/null || echo cd-failed
echo $$ | grep -c '^[0-9][0-9]*$'
