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
)

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
	ctx := context.Background()
	downloads, err := downloadManyGzipedTars(ctx, []string{"https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz"})
	fmt.Println("downloads done, unzipping")

	if err != nil {
		fmt.Println(err)
		return
	}

	for _, d := range downloads {
		fmt.Println(untar(d.r))
		// var err error
		// for err == nil {
		// 	var header *tar.Header
		// 	header, err = d.r.Next()
		// 	if err != nil {
		// 		fmt.Println("err", err)
		// 		continue
		// 	}
		//
		// 	data := make([]byte, header.Size)
		// 	fmt.Println(
		// 		d.r.Read(data),
		// 	)
		//
		// 	fmt.Println(header)
		// }
	}

	fmt.Printf("%#v %#v\n", downloads, err)
}
