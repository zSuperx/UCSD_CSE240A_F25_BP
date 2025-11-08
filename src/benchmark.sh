set -e

tempdir=$(mktemp -d)

make
for trace in ../traces/*.bz2;
do
  echo "Started $trace"
  echo -e "$(basename $trace): $(bzcat $trace | ./predictor $1 | tail -n 1)" >> $tempdir/$(basename $trace) &
done

wait

cat $tempdir/*

rm -rf $tempdir
