echo hello world
echo "quoted   spaces" 'single  $HOME'
A=5
echo $A ${A}x "$A"
false
echo status=$?
true && echo and-ok
false || echo or-ok
false && echo SHOULD-NOT-PRINT
true || echo SHOULD-NOT-PRINT
echo one; echo two
echo -n no-newline; echo
X="a  b   c"
echo $X
echo "$X"
echo ''
echo a\ b
nonexistent_cmd_xyz 2>/dev/null
echo notfound=$?
echo '#notcomment' # real comment
