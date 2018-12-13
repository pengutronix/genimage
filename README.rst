==================================
Genimage - The Image Creation Tool
==================================

genimage is a tool to generate multiple filesystem and flash/disk images
from a given root filesystem tree. genimage is intended to be run
in a fakeroot environment.
It also supports creating flash/disk images out of different file-system images and files.

Configuration is done in a config file parsed by libconfuse. Options
like the path to tools can be given via environment variables, the config
file or from commandline switches.

The Configuration File
======================

The config file of genimage uses a simple configuration language, provided by `libconfuse`_.
This supports nested sections, as well as simple key-value pairs.

.. _libconfuse: https://github.com/martinh/libconfuse

Single-line comments can be introduced with ``#`` or ``//``,
multi-line comments look like ``/* … */`` (as in C).

The config file is separated into the main sections ``image``, ``flash`` and ``config``,
and provides an ``include`` primitive.

The image section
-----------------

An image section describes a single filesystem or disk image to be built. It can be given
multiple times to generate multiple images. An image can also have multiple
partitions which refer to images themselves.
Each image must have a type which can have different suboptions depending on
the type.

Let's have a look at an example::

  image nand-pcm038.img {
	  flash {
	  }
	  flashtype = "nand-64M-512"
	  partition barebox {
		  image = "barebox-pcm038.bin"
		  size = 512K
	  }
	  partition root {
		  image = "root-nand.jffs2"
		  size = 24M
	  }
  }

This would generate a nand-pcm038.img which is a flash of type "nand-64M-512"
The image contains two partitions, "barebox-pcm038.bin" and "root-nand.jffs2"
which must refer to images described elsewhere in the config file. For example
"root-nand.jffs2" partition could be described like this::

  image root-nand.jffs2 {
	  name = "root"
	  jffs2 {}
	  size = 24M
	  mountpoint = "/"
  }

In this case a single jffs2 image is generated from the root mountpoint.

Here are all options for images:

:name:		The name of this image. This is used for some image types
		to set the name of the image.
:size:		Size of this image in bytes
:mountpoint:	mountpoint if image refers to a filesystem image. The
		default is "/". The content of "${rootpath}${mountpoint}"
		will be used used fill the filesystem.
:exec-pre:	Custom command to run before generating the image.
:exec-post:	Custom command to run after generating the image.
:flashtype:	refers to a flash section. Optional for non flash like images
		like hd images
:partition:	can be given multiple times and refers to a partition described
		below

Additionally each image can have one of the following sections describing the
type of the image:

cpio, cramfs, ext2, ext3, ext4, file, flash, hdimage, iso, jffs2, qemu, squashfs,
tar, ubi, ubifs, vfat.

Partition options:

:offset:		The offset of this partition as a total offset to the beginning
			of the device.
:size:			The size of this partition in bytes. The last partition may have
			size 0 to make this partition use the rest of the available space
			on the device.
:partition-type:	Used by dos partition tables to specify the partition type.
:image:			The image file this partition shall be filled with
:autoresize:		used by ubi (FIXME: do we need this? Isn't size = 0 enough)
:bootable:		Boolean specifying whether to set the bootable flag.
:in-partition-table:	Boolean specifying whether to include this partition in
			the partition table.

The image configuration options
-------------------------------

cpio
****
Generates cpio images.

Options:

:format:		archive format. Passed to the ``-H`` option to the cpio command.
			Valid options are ``bin``, ``odc``, ``newc``, ``crc``, ``tar``,
			``ustar``, ``hpbin`` and ``hpodc``
:extraargs:		Extra arguments passed to the cpio tool
:compress:		If given, pipe image through compression tool. Valid options are
			for example ``gzip``, ``lzop`` or any other tool that compresses
			from stdin to stdout.

cramfs
******
Generates cramfs images.

Options:

:extraargs:		Extra arguments passed to mkcramfs

ext2, ext3, ext4
****************
Generates ext* images.

Options:

:extraargs:		Extra arguments passed to genext2fs
:features:		Filesystem features. Passed to the ``-O`` option of tune2fs. This
			is a comma separated list of enabled or disabled features. See
			``man tune2fs`` for features. Default for ext3 images is
			``has_journal``. Default for ext4 images is
			``extents,uninit_bg,dir_index,has_journal``.
:label:			Specify the volume-label. Passed to the ``-L`` option of tune2fs
:fs-timestamp:		Sets different timestamps in the image. Sets the given timestamp
			using the debugfs commands ``set_current_time``,
			``set_super_value mkfs_time`` and ``set_super_value lastcheck``

FIT
***
Generates U-Boot FIT images.

Options:

:its:			String option holding the path of the input its file
:keydir:		String option holding the directory containing the keys
			used for signing.

flash
*****
Generates flash images. These are basically the partition contents padded to the
partition sizes concatenated together. There is no partition table. Needs a valid
flashtype where the flash parameters are read from.

hdimage
*******
Generates DOS partition images.

Options:

:align:			Partition alignment. Defaults to 512 bytes
:partition-table:	Boolean. If true, writes a DOS partition table. If false, no
			partition table is generated. Defaults to true.
:extended-partition:	Number of the extended partition. Contains the number of the
			extended partition between 1 and 4 or 0 for automatic. Defaults
			to 0.
:disk-signature:	32 bit integer used as disk signature (offset 440 in the MBR)

iso
***
Generates an ISO image.

Options:

:boot-image:		Path to the El Torito boot image. Passed to the ``-b`` option
			of genisofs
:bootargs:		Bootargs for the El Torito boot image. Defaults to
			``-no-emul-boot -boot-load-size 4 -boot-info-table -c boot.cat -hide boot.cat``
:extraargs:		Extra arguments passed to genisofs
:input-charset:		The input charset. Passed to the -input-charset option of genisofs.
			Defaults to ``default``
:volume-id:		Volume ID. Passed to the ``-V`` option of genisofs

jffs2
*****
Generates a JFFS image. Needs a valid flashtype where the flash parameters are
read from.

Options:

:extraargs:		Extra arguments passed to mkfs.jffs2

qemu
****
Generates a QEMU image. Needs at least one valid partition.

Options:

:format:		A valid ``qemu-img`` like ``qcow``, ``qcow2``, ``parallels``, ``vdi``,
			``vhdx`` or ``vmdk``. Check ``qemu-img convert --help`` for the complete
			list of possible values. Defaults to ``qcow2``.
:extraargs:		Extra arguments passed to ``qemu-img convert``

squashfs
********
Generates a squashfs image.

Options:

:extraargs:		Extra arguments passed to mksquashfs
:compression:		compression type for the image. Possible values are ``gzip``
			(default), ``lzo``, ``xz`` or ``none``
:block-size:		Block size. Passed to the ``-b`` option of mksquashfs. Defaults
			to 4096.

rauc
****
Generates a RAUC update bundle.

Options:

:extraargs:		Extra arguments passed to RAUC
:file:			Specify a file to be added into the RAUC bundle. Usage is:
			``file foo { image = "bar" }`` which adds a file "foo" in the
			RAUC bundle from then input file "bar"
:files:			A list of filenames added into the RAUC bundle. Like **file**
			above, but without the ability to add the files under different
			name.
:key:			Path to the key file. Passed to the ``--key`` option of RAUC
:cert:			Path to the certificate file. Passed to the ``--cert`` option
			of RAUC
:manifest:		content of the manifest file

tar
***

Generates a tar image. The image will be compressed as defined by the filename suffix.

ubi
***
Generates an UBI image. Needs a valid flashtype where the flash parameters are
read from.

Options:

:extraargs:		Extra arguments passed to ubinize

ubifs
*****
Generates a UBIFS image. Needs a valid flashtype where the flash parameters are
read from.

Options:

:extraargs:		Extra arguments passed to mkubifs
:max-size:		Maximum size of the UBIFS image

vfat
****
Generates a VFAT image.

Options:

:extraargs:		Extra arguments passed to mkdosfs
:file:			Specify a file to be added into the filesystem image. Usage is:
			``file foo { image = "bar" }`` which adds a file "foo" in the
			filesystem image from the input file "bar"
:files:			A list of filenames added into the filesystem image. Like **file**
			above, but without the ability to add the files under different
			name.

Note: If no content is specified with ``file`` or ``files`` then
``rootpath`` and ``mountpoint`` are used to provide the content.

The Flash Section
-----------------

The flash section can be given multiple times and each section describes a
flash chip. The option names are mostly derived from the UBI terminology.
There are the following options:

:pebsize:		The size of a physical eraseblock in bytes
:lebsize:		The size of a logical eraseblock in bytes (for ubifs)
:numpebs:		Number of physical eraseblocks on this device. The total
			size of the device is determined by pebsize * numpebs
:minimum-io-unit-size:	The minimum size in bytes accessible on this device
:vid-header-offset:	offset of the volume identifier header
:sub-page-size:		The size of a sub page in bytes.

Several flash related image types need a valid flash section. From the image types
the flash type section is referred to using the ``flashtype`` option which contains
the name of the flash type to be used.

For more information of the meaning of these values see the ubi(fs) and mtd FAQs:

http://www.linux-mtd.infradead.org/faq/general.html

Example flash section::

  flash nand-64M-512 {
	  pebsize = 16384
	  lebsize = 15360
	  numpebs = 4096
	  minimum-io-unit-size = 512
	  vid-header-offset = 512
	  sub-page-size = 512
  }
  ...
  image jffs2 {
	  flashtype = "nand-64M-512"
  }


The config section
------------------

In this section the global behaviour of the program is described. All options
here can be given from either environment variables, the config file or
command line switches. For instance, a config option ``foo`` can be passed as a
``--foo`` command line switch or as a GENIMAGE_FOO environment variable.

:config:	default: ``genimage.cfg``
		Path to the genimage config file.

:loglevel:	default: 1
		genimage log level.

:outputpath:	default: images
		Mandatory path where all images are written to (must exist).
:inputpath:	default: input
		This mandatory path is searched for input images, for example
		bootloader binaries, kernel images (must exist).
:rootpath:	default: root
		Mandatory path to the root filesystem (must exist).
:tmppath:	default: tmp
		Optional path to a temporary directory. There must be enough space
		available here to hold a copy of the root filesystem.

:cpio:		path to the cpio program (default cpio)
:dd:		path to the dd program (default dd)
:e2fsck:	path to the e2fsck program (default e2fsck)
:genext2fs:	path to the genext2fs program (default genext2fs)
:genisoimage:	path to the genisoimage program (default genisoimage)
:mcopy:		path to the mcopy program (default mcopy)
:mmd:		path to the mmd program (default mmd)
:mkcramfs:	path to the mkcramfs program (default mkcramfs)
:mkdosfs:	path to the mkdosfs program (default mkdosfs)
:mkfsjffs2:	path to the mkfs.jffs2 program (default mkfs.jffs2)
:mkfsubifs:	path to the mkfs.ubifs program (default mkfs.ubifs)
:mksquashfs:	path to the mksquashfs program (default mksquashfs)
:qemu-img:	path to the qemu-img program (default qemu-img)
:tar:		path to the tar program (default tar)
:tune2fs:	path to the tune2fs program (default tune2fs)
:ubinize:	path to the ubinize program (default ubinize)


Include Configurations Fragments
--------------------------------

To include a ``"foo.cfg"`` config file, use the following statement::

    include("foo.cfg")

This allows to re-use, for example flash configuration files, across different image configurations.
