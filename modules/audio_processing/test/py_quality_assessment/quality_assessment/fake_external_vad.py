#!/usr/bin/python
import argparse
import numpy as np

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('-i', required=True)
  parser.add_argument('-o', required=True)

  args = parser.parse_args()

  array = np.arange(100, dtype=np.float32)
  array.tofile(open(args.o, 'w'))


if __name__ == '__main__':
  main()
