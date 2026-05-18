package main

import (
	"archive/tar"
	"compress/gzip"
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
)

func exists(file string, directory bool) bool {
	stat, err := os.Stat(file)
	if os.IsNotExist(err) {
		return false
	} else if err != nil {
		return false
	}
	return stat.IsDir() == directory
}

func downloadGziped(ctx context.Context, c *http.Client, doneChan chan downloadResult, url string) {
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		doneChan <- downloadResult{err: err}
		return
	}

	res, err := c.Do(req)
	if err != nil {
		doneChan <- downloadResult{err: err}
		return
	}

	rGzip, err := gzip.NewReader(res.Body)
	if err != nil {
		doneChan <- downloadResult{err: err}
		return
	}

	rTar := tar.NewReader(rGzip)
	doneChan <- downloadResult{download: download{
		r:    rTar,
		gzip: rGzip,
		req:  req,
	}}
}

type download struct {
	r    *tar.Reader
	gzip *gzip.Reader
	req  *http.Request
}

type downloadResult struct {
	err      error
	download download
}

func untar(r *tar.Reader) error {
	var err error
	for err == nil {
		var header *tar.Header
		header, err = r.Next()
		if err != nil {
			break
		}

		fileInfo := header.FileInfo()

		switch header.Typeflag {
		case tar.TypeDir:
			fmt.Println(header.Name, "created")
			err := os.Mkdir(header.Name, fileInfo.Mode())
			if err != nil {
				return fmt.Errorf("could not create directory %q of tar file: %w", header.Name, err)
			}
		case tar.TypeReg:
			fmt.Println(header.Name, fileInfo.Mode())
			file, err := os.OpenFile(header.Name, os.O_CREATE|os.O_RDWR|os.O_TRUNC, fileInfo.Mode())
			if err != nil {
				return fmt.Errorf("could not create file %q of tar file: %w", header.Name, err)
			}

			_, err = file.ReadFrom(r)
			if err != nil {
				return fmt.Errorf("could write to file from tar %q of tar file: %w", header.Name, err)
			}
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
				errs = append(errs, result.err)
			} else {
				downloads = append(downloads, result.download)
			}
		case <-ctx.Done():
			break wait
		}
	}

	return downloads, errors.Join(errs...)
}

func main() {
	downloadTasks := []struct {
		url       string
		directory string
	}{
		{
			url:       "https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz",
			directory: "limine-binary",
		},
		{
			url:       "https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/edk2-ovmf.tar.gz",
			directory: "edk2-ovmf",
		},
	}

	urls := []string{}

	for _, task := range downloadTasks {
		stat, err := os.Stat(task.directory)
		if os.IsNotExist(err) {
			urls = append(urls, task.url)
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
		err = untar(d.r)
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
}
