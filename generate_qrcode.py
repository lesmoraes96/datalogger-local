import qrcode


# Grafana dashboard URL
dashboard_url = "https://lesmoraes.grafana.net/public-dashboards/e4933e3c55714b679a5978b967fe1f29"


# Generate QR Code
qr = qrcode.QRCode(
    version=1,
    box_size=10,
    border=5
)
qr.add_data(dashboard_url)
qr.make(fit=True)


# Create image
img = qr.make_image(fill_color="black", back_color="white")
img.save("grafana_dashboard_qr.png")


print("QR Code generated successfully!")
