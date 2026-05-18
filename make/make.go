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
	"-z max-page-size=0x1000",
	"--gc-sections",
}

var ldflagsAmd64 = []string{
	"-m", "elf_x86_64",
	"-T", "linker-scripts/x86_64.lds",
}

var cppflags = []string{
	"-I", "src",
	"-I", "limine-protocol",
	"-isystem", "freestnd-c-hdrs/include",
}

func buildCflags() []string {
	return slices.Concat(cflags, cflagsAmd64)
}

func buildAssembly(directory string) {
	assemblyDirectory := os.DirFS(directory)

	matches, err := fs.Glob(assemblyDirectory, "**/*.S")
	if err != nil {
		panic(err)
	}

	doneChan := make(chan error)
	spawned := 0

	for _, match := range matches {
		spawned += 1
		go func() {
			cmd := exec.Command(
				"cc",
				slices.Concat(
					buildCflags(),
					[]string{"-c", match, "-o", match + ".o"},
				)...,
			)
			doneChan <- cmd.Run()
		}()
	}
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
			urls = append(urls, task.url)
			urlMap[task.url] = task
			continue
		} else if err != nil {
			fmt.Printf("could not stat %s: %v", task.directory, err)
			continue
		}
		if !stat.IsDir() {
			fmt.Printf("expected %s to be a directory or to not exist\n", task.directory)
			continue
		}
		fmt.Printf("skipping download of %s\n", task.directory)
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

		fmt.Println("building limine")
		err := cmd.Run()
		if err != nil {
			fmt.Printf("could not build limine: %v\n", err)
			os.Exit(1)
		}
	} else {
		fmt.Println("skipping build of limine")
	}

	fmt.Println("deps downloaded")
}
