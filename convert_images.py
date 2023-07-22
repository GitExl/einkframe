# MIT License

# Copyright (c) 2023 Dennis Meuwissen

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

from os.path import splitext, basename
from glob import glob
from sys import exit
from PIL import Image


COLOR_TYPE_BW = 0
COLOR_TYPE_BWR = 1

ORIENTATION_PORTRAIT = 0
ORIENTATION_LANDSCAPE = 1

COLOR_REMAP = [
  0x00,
  0x03,
  0x04
]


def compress_output_sequence(count, value, data):
  if count >= 0xFFFF:
    print('RLE length too far.')
    exit(-1)

  elif count >= 0xFF:
    data.append(0xFF)
    data.append(count & 0xFF)
    data.append((count >> 8) & 0xFF)
    data.append(value)

  elif count > 0:
    data.append(count)
    data.append(value)


def compress_data(input):
  output = []

  last_value = input[0]
  count = 0
  for value in input:
    if value != last_value:
      compress_output_sequence(count, last_value, output)
      last_value = value
      count = 1
    else:
      count += 1

  compress_output_sequence(count, last_value, output)

  output.append(0xFF)
  output.append(0xFF)
  output.append(0xFF)

  return output


def convert_png_to_bin(path_input):
  print('Converting {}'.format(path_input))
  image = Image.open(path_input)

  color_count = len(image.getcolors())
  if color_count == 2:
    color_type = COLOR_TYPE_BW
    print('Black & white image.')
  elif color_count == 3:
    color_type = COLOR_TYPE_BWR
    print('Black, white and red image.')
  else:
    print('Not a two or three color paletted image.')
    exit(-1)

  width, height = image.size
  if width == 640 and height == 384:
    orientation = ORIENTATION_LANDSCAPE
    print('Landscape orientation.')
  elif width == 384 and height == 640:
    orientation = ORIENTATION_PORTRAIT
    print('Portrait orientation.')
  else:
    print('Invalid image dimensions. Must be 640x384 or 384x640.')
    exit(-1)

  print('Converting...')
  if orientation == ORIENTATION_PORTRAIT:
    image = image.rotate(90, expand=True)

  image_data = image.getdata()
  output = []
  for src in range(0, 640 * 384, 2):
    color_a = COLOR_REMAP[image_data[src]]
    color_b = COLOR_REMAP[image_data[src + 1]]
    output.append(color_a << 4 | color_b)

  return output


def write_bin_as_code(filename, code_name, data):
  print('Writing as code...')

  with open(filename, 'w') as file:
    file.write('const unsigned char {}[] = {{\n'.format(code_name))

    row_length = 0
    file.write('    ');
    for value in data:
      file.write('0x{:02X}, '.format(value))
      row_length += 1
      if row_length > 16:
        file.write('\n    ')
        row_length = 0

    file.write('\n};\n')


def write_bin(filename, data):
  print('Writing as binary...')

  with open(filename, 'wb') as file:
    file.write(bytes(data))


ORIENTATIONS = ['portrait', 'landscap']

# Generate binary images.
for orientation in ORIENTATIONS:
  for path_input in glob('pictures/{}/*.png'.format(orientation)):
    base_name = splitext(basename(path_input))[0]
    path_output = 'sd/{}/{}.bin'.format(orientation, base_name)

    data = convert_png_to_bin(path_input)
    write_bin(path_output, data)

# Generate status image code files.
for orientation in ORIENTATIONS:
  for path_input in glob('pictures/status/{}/*.png'.format(orientation)):
    base_name = splitext(basename(path_input))[0]
    path_output = '{}_{}.c'.format(base_name, orientation)
    code_name = 'IMAGE_DATA_{}_{}'.format(base_name.upper(), orientation.upper())

    data = convert_png_to_bin(path_input)
    data = compress_data(data)
    write_bin_as_code(path_output, code_name, data)
