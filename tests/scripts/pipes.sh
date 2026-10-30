echo hello | tr a-z A-Z
printf 'b\na\nc\n' | sort | head -2
printf '1\n2\n3\n' | wc -l | tr -d ' '
echo abc | cat | cat | cat
yes | head -3
false | true
echo last-wins=$?
true | false
echo last-fails=$?
printf 'x\ny\n' | grep y | tr y Y
