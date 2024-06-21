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

.. _libconfuse: https://github.com/libconfuse/libconfuse

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
:size:		Size of this image in bytes. 'k', 'M' or 'G' can be used as
		suffix to specify the size in multiple of 1024
		etc. The suffix 's' specifies a multiple of the
		(traditional) sector size of 512. If the image if
		filled from a mountpoint then '%' as suffix indicates
		a percentage. '200%' means the resulting filesystem
		should be about 50% filled. Note that is is only a
		rough estimate based on the original size of the
		content.
:mountpoint:	mountpoint if image refers to a filesystem image. The
		default is "/". The content of "${rootpath}${mountpoint}"
		will be used to fill the filesystem.
:srcpath:	If this is set, specified path will be directly used
		to fill the filesystem. Ignoring rootpath/mountpoint logic.
		Path might be absolute or relative
		to current working directory.
:empty:		If this is set to true, then the specified rootpath and
		mountpoint are ignored for this image and an empty
		filesystem is created. This option is only used for
		writeable filesystem types, such as extX, vfat, ubifs and
		jffs2. This defaults to false.
:temporary:	If this is set to true, the image is created in
		``tmppath`` rather than ``outputpath``. This can be
		useful for intermediate images defined in the
		configuration file which are not needed by themselves
		after the main image is created. This defaults to
		false.
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
:size:			The size of this partition in bytes. If the size and
			autoresize are both not set then the size of the partition
			image is used.
:align:			Alignment value to use for automatic computation of ``offset``
			and ``size``.  Defaults to 1 for partitions not in the partition
			table, otherwise to the image's ``align`` value.
:partition-type:	Used by dos partition tables to specify the partition type.
:image:			The image file this partition shall be filled with
:fill:			Boolean specifying that all bytes of the partition should be
			explicitly initialized. Any bytes beyond the size of the specified
			image will be set to 0.
:autoresize:		Boolean specifying that the partition should be resized
			automatically. For UBI volumes this means that the
			``autoresize`` flag is set. Only one volume can have this flag.
			For hd images this can be used for the last partition. If set
			the partition will fill the remaining space of the image.
:bootable:		Boolean specifying whether to set the bootable flag.
:in-partition-table:	Boolean specifying whether to include this partition in
			the partition table. Defaults to true.
:forced-primary:	Force this partition to be a primary partition in the
			MBR partition table, useful when the extended partition should be
			followed by primary partitions. If there are more partitions
			defined after the first forced-primary, they must be also defined
			as forced-primary. Defaults to false.
:partition-uuid:	UUID string used by GPT partition tables to specify the partition
			id. Defaults to a random value.
:partition-type-uuid:	String used by GPT partition tables to specify the partition type.
			Either a UUID or a shortcut:

			* ``L``, ``linux``, ``linux-generic``: Linux filesystem (0fc63daf-8483-4772-8e79-3d69d8477de4)
			* ``S``, ``swap``: Swap (0657fd6d-a4ab-43c4-84e5-0933c84b4f4f)
			* ``H``, ``home``: Home (933ac7e1-2eb4-4f13-b844-0e14e2aef915)
			* ``U``, ``esp``, ``uefi``: EFI System Partition (c12a7328-f81f-11d2-ba4b-00a0c93ec93b)
			* ``R``, ``raid``: Linux RAID (a19d880f-05fc-4d3b-a006-743f0f84911e)
			* ``V``, ``lvm``: Linux LVM (e6d6d379-f507-44c2-a23c-238f2a3df928)
			* ``F``, ``fat32``: FAT32 / Basic Data Partition (ebd0a0a2-b9e5-4433-87c0-68b6b72699c7)
			* ``barebox-state`` (previously ``B``): Barebox State (4778ed65-bf42-45fa-9c5b-287a1dc4aab1)
			* ``barebox-env``: Barebox Environment (6c3737f2-07f8-45d1-ad45-15d260aab24d)

                        Furthermore, for ``{arch}`` being one of ``alpha``,
                        ``arc``, ``arm``, ``arm64``, ``ia64``, ``loongarch64``,
                        ``mips``, ``mips64``, ``mips-le``, ``mips64-le``, ``parisc``, ``ppc``,
                        ``ppc64``, ``ppc64-le``, ``riscv32``, ``riscv64``,
                        ``s390``, ``s390x``, ``tilegx``, ``x86``, ``x86-64``,
                        the following shortcuts from the `Discoverable
                        Partitions Specification <dps-spec_>`_ are accepted (see the spec
                        for the respective UUIDs):

                        * ``root-{arch}``: Root Partition
                        * ``usr-{arch}``: /usr Partition
                        * ``root-{arch}-verity``: Root Verity Partition
                        * ``usr-{arch}-verity``: /usr Verity Partition
                        * ``root-{arch}-verity-sig``: Root Verity Signature Partition
                        * ``usr-{arch}-verity-sig``: /usr Verity Signature Partition
                        * ``xbootldr``: Extended Boot Loader Partition
                        * ``srv``: Server Data Partition
                        * ``var``: Variable Data Partition
                        * ``tmp``: Temporary Data Partition
                        * ``user-home``: Per-user Home Partition

			Defaults to ``L``.

.. _dps-spec: https://uapi-group.org/specifications/specs/discoverable_partitions_specification/

For each partition, its final alignment, offset and size are determined as follows:

* If the ``align`` option is not present, it defaults to the value of
  the image's ``align`` option if the partition is in the partition
  table, otherwise to 1.

* If the ``offset`` option is absent or zero, and
  ``in-partition-table`` is true, the partition is placed after the
  end of all previously defined partitions, with the final offset
  rounded up to the partition's ``align`` value.

* Otherwise, the ``offset`` option is used as-is. Note that if absent,
  that option defaults to 0, so in practice one must specify an
  ``offset`` for any partition that is not in the partition table
  (with at most one exception, e.g. a bootloader).

* If the partition has the ``autoresize`` flag set, its size is
  computed as the space remaining in the image from its offset (for a
  GPT image, space is reserved at the end for the backup GPT table),
  rounded down to the partition's ``align`` value. If the partition
  also has a ``size`` option, it is ensured that the computed value is
  not less than that size.

* Otherwise, if the ``size`` option is present and non-zero, its value
  is used as-is.

* Otherwise, if the partition has an ``image`` option, the size of
  that image, rounded up to the partition's ``align`` value, is used
  to determine the size of the partition.

The following sanity checks are done on these final values (in many
cases, these will automatically be satisfied when the value has been
determined via one of the above rules rather than given explicitly):

* For a partition in the partition table, the partition's ``align``
  value must be greater than or equal to the image's ``align`` value.

* The partition's ``offset`` and ``size`` must both be multiples of
  its ``align``.

* The size must not be 0.

* The partition must not overlap any other partition, or the areas
  occupied by the partition table.

The image configuration options
-------------------------------

android-sparse
**************
Generate android sparse images. They are typically used by fastboot. Sparse
images encode "don't care" areas and areas that are filled with a single
32 bit value. As a result, they are often much smaller than raw disk
images.
Genimage assumes that all 'holes' in the input file are "don't care" areas.
This is a reasonable assumption: Tools to generate filesystems typically
operate on devices. So they only create holes in areas they don't care
about. Genimage itself operates the same way when generating HD images.

Options:

:image:			The source image that will be converted.
:block-size:		The granularity that the sparse image uses to
			find "don't care" or "fill" blocks. The supported
			block sizes depend on the user. The default is 4k.

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

:use-mke2fs:		If set to true, then mke2fs is used to create the
			image. Otherwise, genext2fs is used. Defaults to false.
:mke2fs-conf:		mke2fs.conf that should be used. If unspecified, the system
			default is used.
:extraargs:		Extra arguments passed to genext2fs or mke2fs.
:features:		Filesystem features. Passed to the ``-O`` option of tune2fs. This
			is a comma separated list of enabled or disabled features. See
			``man ext4`` for features.
			For genext2fs all feature are specified. Default for ext3 images is
			``has_journal``. Default for ext4 images is
			``extents,uninit_bg,dir_index,has_journal``.
			For mke2fs these features are added in addition to the default
			features of the ext type. Already enabled features can be disabled
			by prefixing the feature with ``^``.
:label:			Specify the volume-label. Passed to the ``-L`` option of tune2fs
:fs-timestamp:		Sets different timestamps in the image. Sets the given timestamp
			using the debugfs commands ``set_current_time``,
			``set_super_value mkfs_time`` and ``set_super_value lastcheck``
:root-owner:		User and group IDs for the root directory. Defaults to ``0:0``.
			Only valid with mke2fs.
:usage-type:		Specify the usage type for the filesystem. Only valid with mke2fs.
			More details can be found in the mke2fs man-page.

file
****

This represents a pre-existing image which will be used as-is. When a
partition section references an image that is not defined elsewhere in
the configuration file, a ``file`` rule is implicitly generated. It is
up to the user to ensure that the image exists in the input directory,
or to use an absolute path to the image.

It is possible to add a ``file`` image explicitly, which allows one to
provide ``genimage`` with some information about the image which can
not be deduced automatically. Currently, one such option exists:

:holes:			A list of ``"(<start>;<end>)"`` pairs specifying ranges of the
			file that do not contain meaningful data, and which can therefore
			be allowed to overlap other partitions or image metadata.

For example::

  image foo {
	  hdimage {
		  partition-table-type = "gpt"
		  gpt-location = 64K
	  }

	  partition bootloader {
		  in-partition-table = false
		  offset = 0
		  image = "/path/to/bootloader.img"
	  }

	  partition rootfs {
		  offset = 1M
		  image = "rootfs.ext4"
	  }
  }

  image /path/to/bootloader.img {
	  file {
		  holes = {"(440; 1K)", "(64K; 80K)"}
	  }
  }

This tells ``genimage`` that despite the ``bootloader`` partition
overlapping both the last 72 bytes of the MBR (where the DOS partition
table is located) and the GPT header occupying the sector starting at
offset 512, this is all OK because ``bootloader.img`` does not contain
useful data in that range. Further, in this example, the bootloader
image has been carefully crafted to also allow placing the GPT array
at offset 64K (the GPT header is always at offset 512).

If the bootloader image is not declared explicitly and only used once then
the holes can also be configured in the partition. This simplifies the
config file for simple use-cases.

For example::

  image bar {
	  hdimage {}

	  partition bootloader {
		  in-partition-table = false
		  offset = 0
		  image = "/path/to/bootloader.img"
		  holes = {"(440; 512)"}
	  }

	  partition rootfs {
		  offset = 1M
		  image = "rootfs.ext4"
	  }
  }

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
:partition-table:	Boolean. If true, writes a partition table. If false, no
			partition table is generated. Defaults to true.
			Deprecated: use ``partition-table-type`` instead.
:partition-table-type:	Define what kind of partition table should be used.
			Valid options are:
			 * ``none``: No partition table at all. In this case, the
			   ``in-partition-table`` option for each partition is ignored.
			 * ``mbr``: Legacy DOS/MBR partition table
			 * ``gpt``: GUID Partition Table
			 * ``hybrid``: A hybrid MBR/GPT partition table. Partitions with
			   an explicit `partition-type` will be placed in in the MBR
			   table. At most 3 such partitions are allowed. This limit does
			   not effect the maximum number of GPT partition entries in the
			   same image.
:extended-partition:	Number of the extended partition. Contains the number of the
			extended partition between 1 and 4 or 0 for automatic. Defaults
			to 0.
:disk-signature:	32 bit integer used as disk signature (offset 440 in the
                        MBR). Using a special value ``random`` will result in
                        using random 32 bit number.
:gpt:			Boolean. If true, a GPT type partition table is written. If false
			a DOS type partition table is written. Defaults to false.
			Deprecated: use ``partition-table-type`` instead.
:gpt-location:		Location of the GPT table. Occasionally useful for moving the GPT
			table away from where a bootloader is placed due to hardware
			requirements.  All partitions in the table must begin after this
			table.  Regardless of this setting, the GPT header will still be
			placed at 512 bytes (sector 1).  Defaults to 1024 bytes (sector 2).
:gpt-no-backup:         Boolean. If true, then the backup partition table at the end of
                        the image is not written.
:disk-uuid:		UUID string used as disk id in GPT partitioning. Defaults to a
			random value.
:fill:			If this is set to true, then the image file will be filled
			up to the end of the last partition. This might make the file
			bigger. This is necessary if the image will be processed by
			such tools as libvirt, libguestfs or parted.

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
:key:			Path to the key file or PKCS#11 URI. Passed to the ``--key`` option of
			RAUC
:cert:			Path to the certificate file or PKCS#11 URI. Passed to the ``--cert``
			option of RAUC
:keyring:		Optional path to the keyring file. Passed to the ``--keyring``
			option of RAUC
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
:space-fixup:           Instructs the file-system free space to be freed up on first mount.

vfat
****
Generates a VFAT image.

Options:

:extraargs:		Extra arguments passed to mkdosfs
:label:		Specify the volume-label. Passed to the ``-n`` option of mkdosfs
:file:			Specify a file to be added into the filesystem image. Usage is:
			``file foo { image = "bar" }`` which adds a file "foo" in the
			filesystem image from the input file "bar"
:files:			A list of filenames added into the filesystem image. Like **file**
			above, but without the ability to add the files under different
			name.

Note: If no content is specified with ``file`` or ``files`` then
``rootpath`` and ``mountpoint`` are used to provide the content.

fip
***
Generates a Firmware Image Package (FIP). A format used to bundle
firmware to be loaded by ARM Trusted Firmware.

Options:

:extraargs:		Extra arguments passed to fiptool
:fw-config:		Firmware Configuration (device tree), usually provided by BL2 (Trusted Firmware)
:nt-fw:			Non-Trusted Firmware (BL33)
:hw-config:		Hardware Configuration (device tree), passed to BL33
:tos-fw:		Trusted OS (BL32) binaries. Second and third binary are used as
			extra1 and extra2 binaries if specified. Example:
			``tos-fw = {"tee-header_v2.bin", "tee-pager_v2.bin", "tee-pageable_v2.bin"}``
:scp-fwu-cfg:		SCP Firmware Updater Configuration FWU SCP_BL2U
:ap-fwu-cfg:		AP Firmware Updater Configuration BL2U
:fwu:			Firmware Updater NS_BL2U
:fwu-cert:		Non-Trusted Firmware Updater certificate
:tb-fw:			Trusted Boot Firmware BL2
:scp-fw:		SCP Firmware SCP_BL2
:soc-fw:		EL3 Runtime Firmware BL31
:tb-fw-config:		TB_FW_CONFIG
:soc-fw-config:		SOC_FW_CONFIG
:tos-fw-config:		TOS_FW_CONFIG
:nt-fw-config:		NT_FW_CONFIG
:rot-cert:		Root Of Trust key certificate
:trusted-key-cert:	Trusted key certificate
:scp-fw-key-cert:	SCP Firmware key certificate
:soc-fw-key-cert:	SoC Firmware key certificate
:tos-fw-key-cert:	Trusted OS Firmware key certificate
:nt-fw-key-cert:	Non-Trusted Firmware key certificate
:tb-fw-cert:		Trusted Boot Firmware BL2 certificate
:scp-fw-cert:		SCP Firmware content certificate
:soc-fw-cert:		SoC Firmware content certificate
:tos-fw-cert:		Trusted OS Firmware content certificate
:nt-fw-cert:		Non-Trusted Firmware content certificate
:sip-sp-cert:		SiP owned Secure Partition content certificate
:plat-sp-cert:		Platform owned Secure Partition content certificate

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

In this section the global behaviour of the program is
described. Except as noted below, all options here can be given from
either environment variables, the config file or command line
switches. For instance, a config option ``foo`` can be passed as a
``--foo`` command line switch or as a GENIMAGE_FOO environment
variable.

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
:includepath:	Colon-separated list of directories to search for files
		included via the ``include`` function. The current
		directory is searched after these. Thus, if this
		option is not given, only the current directory is
		searched. This has no effect when given in the config file.
:configdump:	File to write the final configuration to. This includes
		the results of all ``include`` directives, expansions
		of environment variables and application of default
		values - think ``gcc -E``. Use ``-`` for stdout.

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
:fiptool:	path to the fiptool utility (default fiptool)


Include Configurations Fragments
--------------------------------

To include a ``"foo.cfg"`` config file, use the following statement::

    include("foo.cfg")

This allows to re-use, for example flash configuration files, across different image configurations.

License and Developing
======================

To contribute to genimage please prepare a pull request on Github. To make
it possible to include your modifications it's required that your code
additions are licensed under the same terms as genimage itself. So you
are required to agree to the following document:

  Developer's Certificate of Origin 1.1

  By making a contribution to this project, I certify that:

  (a) The contribution was created in whole or in part by me and I
      have the right to submit it under the open source license
      indicated in the file; or

  (b) The contribution is based upon previous work that, to the best
      of my knowledge, is covered under an appropriate open source
      license and I have the right under that license to submit that
      work with modifications, whether created in whole or in part
      by me, under the same open source license (unless I am
      permitted to submit under a different license), as indicated
      in the file; or

  (c) The contribution was provided directly to me by some other
      person who certified (a), (b) or (c) and I have not modified
      it.

  (d) I understand and agree that this project and the contribution
      are public and that a record of the contribution (including all
      personal information I submit with it, including my sign-off) is
      maintained indefinitely and may be redistributed consistent with
      this project or the open source license(s) involved.

Your agreement is expressed by adding a sign-off line to each of your
commits (e.g. using ``git commit -s``) looking as follows:

        Signed-off-by: Random J Developer <random@developer.example.org>

with your identity and email address matching the commit meta data.
