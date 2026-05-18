package main

import (
	"archive/tar"
	"compress/gzip"
	"context"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"
	"time"
)

func downloadGziped(ctx context.Context, c *http.Client, doneChan chan downloadResult, url string) {
	var download download
	var err error

	download.req, err = http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		doneChan <- downloadResult{err: err, download: download}
		return
	}

	download.res, err = c.Do(download.req)
	if err != nil {
		doneChan <- downloadResult{err: err, download: download}
		return
	}

	var responseBody strings.Builder
	download.body = &responseBody

	tee := io.TeeReader(download.res.Body, &responseBody)

	download.gzip, err = gzip.NewReader(tee)
	if err != nil {
		doneChan <- downloadResult{err: err, download: download}
		return
	}

	download.r = tar.NewReader(download.gzip)
	doneChan <- downloadResult{download: download}
}

type download struct {
	r    *tar.Reader
	gzip *gzip.Reader
	body *strings.Builder
	req  *http.Request
	res  *http.Response
}

type downloadResult struct {
	err      error
	download download
}

func untar(r *tar.Reader, targetDirectory, removePrefix string) error {
	var err error
	for err == nil {
		var header *tar.Header
		header, err = r.Next()
		if err != nil {
			break
		}

		fileInfo := header.FileInfo()
		name := filepath.Join(targetDirectory, strings.TrimPrefix(header.Name, removePrefix))

		switch header.Typeflag {
		case tar.TypeDir:
			fmt.Println(name, "created")
			err := os.Mkdir(name, fileInfo.Mode())
			if err != nil {
				return fmt.Errorf("could not create directory %q of tar file: %w", name, err)
			}
		case tar.TypeReg:
			fmt.Println(name, "created")
			file, err := os.OpenFile(name, os.O_CREATE|os.O_RDWR|os.O_TRUNC, fileInfo.Mode())
			if err != nil {
				return fmt.Errorf("could not create file %q of tar file: %w", name, err)
			}

			_, err = file.ReadFrom(r)
			if err != nil {
				return fmt.Errorf("could write to file from tar %q of tar file: %w", name, err)
			}
		case tar.TypeXGlobalHeader:
		default:
			panic(header.Typeflag)
		}
	}

	if err == io.EOF {
		return nil
	}
	return err
}

func downloadManyGzipedTars(ctx context.Context, urls []string) (downloads []download, err error) {
	var c http.Client
	c.Transport = &http.Transport{
		Proxy: http.ProxyFromEnvironment,
	}

	doneChan := make(chan downloadResult, 5)
	doneUrls := 0
	errs := []error{}

	for _, url := range urls {
		go downloadGziped(ctx, &c, doneChan, url)
	}

wait:
	for doneUrls < len(urls) {
		select {
		case result := <-doneChan:
			doneUrls += 1
			if result.err != nil {
				errs = append(
					errs,
					fmt.Errorf(
						"download task failed %#v: %w - %s",
						result.download,
						result.err,
						result.download.body.String(),
					),
				)
			} else {
				downloads = append(downloads, result.download)
			}
		case <-ctx.Done():
			break wait
		}
	}

	return downloads, errors.Join(errs...)
}

type downloadTask struct {
	url            string
	directory      string
	stripDirectory string
}

func downloadFromGithub(owner, repo, revision, directory string) downloadTask {
	return downloadTask{
		url: fmt.Sprintf(
			"https://github.com/%s/%s/archive/%s.tar.gz",
			owner, repo, revision),
		directory:      directory,
		stripDirectory: repo + "-" + revision,
	}
}

var cflags = []string{
	"-g",
	"-O2",
	"-pipe",
	"-Wall",
	"-Wextra",
	"-Wconversion",
	"-std=gnu11",
	"-nostdinc",
	"-ffreestanding",
	"-fno-stack-protector",
	"-fno-stack-check",
	"-fno-lto",
	"-fno-PIC",
	"-ffunction-sections",
	"-fdata-sections",
}

var cflagsAmd64 = []string{
	"-m64",
	"-march=x86-64",
	"-mabi=sysv",
	"-mno-80387",
	"-mno-mmx",
	"-mno-sse",
	"-mno-sse2",
	"-mno-red-zone",
	"-mcmodel=kernel",
}

var ldflags = []string{
	"-nostdlib",
	"-static",
	"-z", "max-page-size=0x1000",
	"--gc-sections",
}

var ldflagsAmd64 = []string{
	"-m", "elf_x86_64",
	"-T", "linker-scripts/x86_64.lds",
}

var cppflags = []string{
	"-I", "src",
	"-I", "limine-protocol/include",
	"-isystem", "freestnd-c-hdrs/include",
}

func buildCflags() []string {
	return slices.Concat(cflags, cflagsAmd64, cppflags)
}

func buildLdflags() []string {
	return slices.Concat(ldflags, ldflagsAmd64)
}

func ccFile(ctx context.Context, logPrefix string, directory, srcFile, objectFile string) (err error) {
	objectDir := filepath.Join(directory, filepath.Dir(objectFile))

	err = os.MkdirAll(objectDir, 0o755)
	if err != nil {
		fmt.Printf("%s: could not create obj directory: %v\n", logPrefix, err)
	}

	cmd := exec.CommandContext(
		ctx,
		"cc",
		slices.Concat(
			buildCflags(),
			[]string{"-c", srcFile, "-o", objectFile},
		)...,
	)

	cmd.Dir = directory

	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	fmt.Printf("%s %s -> %s\n", logPrefix, srcFile, objectFile)
	err = cmd.Run()

	return
}

func buildAssembly(directory, sourceDirectory, objectDirectory string) (ok bool) {
	assemblyDirectory := os.DirFS(sourceDirectory)

	matches, err := fs.Glob(assemblyDirectory, "**/*.S")
	if err != nil {
		panic(err)
	}

	doneChan := make(chan error)
	spawned := 0

	ctx, ctxCancel := context.WithTimeout(context.Background(), time.Second*5)
	defer ctxCancel()

	for _, match := range matches {
		spawned += 1
		go func() {
			srcFile, err := filepath.Rel(directory, filepath.Join(sourceDirectory, match))
			if err != nil {
				panic(err)
			}
			objectFile, err := filepath.Rel(directory, filepath.Join(objectDirectory, match+".o"))
			if err != nil {
				panic(err)
			}

			err = ccFile(ctx, "ASM", directory, srcFile, objectFile)

			doneChan <- err
		}()
	}

	ok = true

wait:
	for spawned > 0 {
		select {
		case err := <-doneChan:
			if err != nil {
				fmt.Printf("ASM: failed to build: %v\n", err)
				ok = false
			}
			spawned -= 1
		case <-ctx.Done():
			fmt.Println("canceled asm build as it took longer than 5 seconds")
			ok = false
			break wait
		}
	}

	return
}

func buildMain() bool {
	err := ccFile(context.Background(), "CC", "kernel", "src/main.c", "obj-x86_64/main.c.o")

	if err != nil {
		fmt.Printf("CC: failed to build: %v\n", err)
		return false
	}
	return true
}

func linkKernel() bool {
	ctx, ctxCancel := context.WithTimeout(context.Background(), time.Second*5)
	defer ctxCancel()

	objectDir := "kernel/bin-x86_64"

	err := os.MkdirAll(objectDir, 0o755)
	if err != nil {
		fmt.Printf("LD: could not create bin directory: %v\n", err)
	}

	objFs := os.DirFS("kernel/obj-x86_64")
	objectFiles, err := fs.Glob(objFs, "**/*.o")
	if err != nil {
		fmt.Printf("LD: could not find object files: %v\n", err)
		return false
	}

	for i, objectFile := range objectFiles {
		objectFiles[i] = filepath.Join("obj-x86_64", objectFile)
	}

	binaryFile := "bin-x86_64/kernel"

	cmd := exec.CommandContext(
		ctx,
		"ld",
		slices.Concat(
			buildLdflags(),
			objectFiles,
			[]string{"-o", binaryFile},
		)...,
	)

	cmd.Dir = "kernel"

	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	fmt.Printf("LD %v -> %s\n", objectFiles, binaryFile)
	err = cmd.Run()

	if err != nil {
		fmt.Printf("LD: failed to build: %v\n", err)
		return false
	}

	return true
}

func createImage() bool {
	const isoRoot = "iso_root"

	if err := os.RemoveAll(isoRoot); err != nil {
		fmt.Printf("IMAGE: could not remove iso root: %v\n", err)
		return false
	}

	mkdir := func(dir string) bool {
		if err := os.MkdirAll(filepath.Join(isoRoot, dir), 0o755); err != nil {
			fmt.Printf("IMAGE: could not %s: %v\n", isoRoot+dir, err)
			return false
		}
		return true
	}

	link := func(from, to string) bool {
		isoRootTo := filepath.Join(isoRoot, to)
		if err := os.Link(from, isoRootTo); err != nil {
			fmt.Printf("IMAGE: could not hardlink %s -> %s: %v\n", from, isoRootTo, err)
			return false
		}
		return true
	}

	if !mkdir("boot") {
		return false
	}

	if !link("kernel/bin-x86_64/kernel", "boot/kernel") {
		return false
	}

	if !mkdir("boot/limine") {
		return false
	}

	if !link("limine.conf", "boot/limine/limine.conf") {
		return false
	}

	if !mkdir("EFI/BOOT") {
		return false
	}

	limines := []string{
		"limine-bios.sys", "limine-bios-cd.bin", "limine-uefi-cd.bin",
	}

	for _, limine := range limines {
		if !link("limine-binary/"+limine, "boot/limine/"+limine) {
			return false
		}
	}

	if !link("limine-binary/BOOTX64.EFI", "EFI/BOOT/BOOTX64.EFI") {
		return false
	}

	if !link("limine-binary/BOOTIA32.EFI", "EFI/BOOT/BOOTIA32.EFI") {
		return false
	}

	xorrisoCommand := exec.Command(
		"xorriso",
		"-as", "mkisofs", // mode
		"-R", "-r", "-J", // extensions

		// Legacy boot
		"-b", "boot/limine/limine-bios-cd.bin",
		"-no-emul-boot", "-boot-load-size", "4",
		"-boot-info-table",

		// Mac
		"-hfsplus", "-apm-block-size", "2048",

		// UEFI boot
		"--efi-boot", "boot/limine/limine-uefi-cd.bin",
		"-efi-boot-part", "--efi-boot-image",

		// USB
		"--protective-msdos-label",

		"iso_root", "-o", "drastic.iso",
	)

	var output strings.Builder
	xorrisoCommand.Stdout = &output
	xorrisoCommand.Stderr = &output

	err := xorrisoCommand.Run()
	if err != nil {
		fmt.Printf("IMAGE: could not run xorriso: %v", err)
		fmt.Printf("IMAGE: xorriso output:\n")
		fmt.Println(output.String())
		return false
	}

	limineCommand := exec.Command(
		"./limine-binary/limine",
		"bios-install",
		"drastic.iso",
	)

	output.Reset()
	limineCommand.Stdout = &output
	limineCommand.Stderr = &output

	err = limineCommand.Run()
	if err != nil {
		fmt.Printf("IMAGE: could not run limine: %v", err)
		fmt.Printf("IMAGE: limine output:\n")
		fmt.Println(output.String())
		return false
	}

	fmt.Println("IMAGE: iso_root -> drastic.iso")

	if err := os.RemoveAll(isoRoot); err != nil {
		fmt.Printf("IMAGE: could not remove iso root: %v\n", err)
		return false
	}

	return true
}

func runQemu() {
	qemuCommand := exec.Command(
		"qemu-system-x86_64",
		"-M", "q35",
		"-drive", "if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-x86_64.fd,readonly=on",
		"-cdrom", "drastic.iso",
		"-m", "2G", "-serial", "mon:stdio",
	)

	qemuCommand.Stdout = os.Stdout
	qemuCommand.Stderr = os.Stderr
	qemuCommand.Stdin = os.Stdin

	qemuCommand.Run()
}

func main() {
	downloadTasks := []downloadTask{
		{
			url:            "https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz",
			directory:      "limine-binary",
			stripDirectory: "limine-binary",
		},
		{
			url:            "https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/edk2-ovmf.tar.gz",
			directory:      "edk2-ovmf",
			stripDirectory: "edk2-ovmf",
		},
		downloadFromGithub(
			"osdev0",
			"freestnd-c-hdrs-0bsd",
			"097259a899d30f0a4b7a694de2de5fdda942e923",
			"kernel/freestnd-c-hdrs",
		),
		downloadFromGithub(
			"osdev0",
			"cc-runtime",
			"dae79833b57a01b9fd3e359ee31def69f5ae899b",
			"kernel/cc-runtime",
		),
		downloadFromGithub(
			"Limine-Bootloader",
			"limine-protocol",
			"80ef54bed402b8c0b672a707c1df4c532f3428ad",
			"kernel/limine-protocol",
		),
	}

	urls := []string{}
	urlMap := make(map[string]downloadTask)

	for _, task := range downloadTasks {
		stat, err := os.Stat(task.directory)
		if os.IsNotExist(err) {
			fmt.Printf("DOWNLOAD %s -> %s\n", task.url, task.directory)

			urls = append(urls, task.url)
			urlMap[task.url] = task
			continue
		} else if err != nil {
			fmt.Printf("DOWNLOAD %s -> %s ERR\n", task.url, task.directory)
			fmt.Printf("could not stat %s: %v", task.directory, err)
			continue
		}
		if !stat.IsDir() {
			fmt.Printf("DOWNLOAD %s -> %s ERR\n", task.url, task.directory)
			fmt.Printf("expected %s to be a directory or to not exist\n", task.directory)
			continue
		}
		fmt.Printf("DOWNLOAD %s -> %s CACHED\n", task.url, task.directory)
	}

	ctx := context.Background()
	downloads, err := downloadManyGzipedTars(ctx, urls)

	if err != nil {
		fmt.Printf("could not start downloads: %v\n", err)
		os.Exit(1)
		return
	}

	for _, d := range downloads {
		task := urlMap[d.req.URL.String()]
		err = untar(d.r, task.directory, task.stripDirectory)
		if err != nil {
			fmt.Printf("could not untar %s: %v\n", d.req.URL, err)
		}
	}

	_, err = os.Stat("limine-binary/limine")
	if os.IsNotExist(err) {
		// build it
		cmd := exec.Command("cc", "-g", "-O2", "-pipe", "-std=c99", "limine-binary/limine.c", "-o", "limine-binary/limine")

		fmt.Println("CC limine-binary/limine.c -> limine-binary/limine")
		err := cmd.Run()
		if err != nil {
			fmt.Printf("could not build limine: %v\n", err)
			os.Exit(1)
		}
	} else {
		fmt.Println("CC limine-binary/limine.c -> limine-binary/limine CACHED")
	}

	objectDirectory := "kernel/obj-x86_64"

	err = os.MkdirAll(objectDirectory, 0o755)
	if err != nil {
		fmt.Printf("ASM: could not create object directory")
		os.Exit(1)
	}

	if !buildAssembly("kernel", "kernel/src", "kernel/obj-x86_64") {
		os.Exit(1)
	}

	if !buildMain() {
		os.Exit(1)
	}

	if !linkKernel() {
		os.Exit(1)
	}

	if !createImage() {
		os.Exit(1)
	}

	runQemu()
}
