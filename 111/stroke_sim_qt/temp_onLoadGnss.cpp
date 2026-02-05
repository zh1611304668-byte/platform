void MainWindow::onLoadGnss() {
  QString file_name = QFileDialog::getOpenFileName(
      this, "Open GNSS Data File", "", "JSONL Files (*.jsonl);;All Files (*)");
  if (file_name.isEmpty()) {
    return;
  }

  worker_->load_gnss_data(file_name);
  QMessageBox::information(
      this, "GNSS Data", QString("GNSS data loaded from:\n%1").arg(file_name));
}
