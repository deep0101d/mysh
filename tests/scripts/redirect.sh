T=/tmp/mysh_redir_$$
mkdir -p $T
echo first > $T/f
echo second >> $T/f
cat < $T/f
cat $T/f | wc -l | tr -d ' '
ls /nonexistent_dir_xyz 2> $T/err || echo ls-failed
cat $T/err | wc -l | tr -d ' '
sh -c 'echo out; echo err 1>&2' > $T/both 2>&1
sort $T/both
echo x > $T/g > $T/h
cat $T/h
cat < /nonexistent_file_xyz 2>/dev/null || echo bad-input-failed
rm -rf $T
