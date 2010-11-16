AC_DEFUN([AX_CONFIG_NICE],[
  config_nice="config.nice"
  
  test -f $config_nice && mv $config_nice $config_nice.old
  rm -f $config_nice.old
  cat >$config_nice <<EOF
#! /bin/sh
#
# Created by configure

EOF

  for var in CC CFLAGS CPP CPPFLAGS CXX CXXFLAGS LDFLAGS LIBS; do
    eval val=\$$var
    if test -n "$val"; then
      echo "$var='$val' \\" >> $config_nice
    fi
  done

  echo "'[$]0' \\" >> $config_nice

  for arg in $ac_configure_args; do
     if test `expr -- $arg : "'.*"` = 0; then
        if test `expr -- $arg : "--.*"` = 0; then
       	  break;
        fi
        echo "'[$]arg' \\" >> $config_nice
     else
        if test `expr -- $arg : "'--.*"` = 0; then
       	  break;
        fi
        echo "[$]arg \\" >> $config_nice
     fi
  done
  echo '"[$]@"' >> $config_nice
  chmod 755 $config_nice
])
