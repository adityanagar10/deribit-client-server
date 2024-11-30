"use client";

import React, { useState, useEffect } from "react";
import ModifyOrderForm from "./ModifyOrderForm";
import { Button } from "@/components/ui/button";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { ScrollArea } from "@/components/ui/scroll-area";

interface Order {
  order_id: string;
  instrument_name: string;
  amount: number;
  price: number;
  direction: string;
  order_type: string;
}

interface OpenOrdersListProps {
  ws: WebSocket | null;
}

export default function OpenOrdersList({ ws }: OpenOrdersListProps) {
  const [openOrders, setOpenOrders] = useState<Order[]>([]);
  const [selectedOrder, setSelectedOrder] = useState<Order | null>(null);
  const [message, setMessage] = useState<{
    type: "success" | "error";
    text: string;
  } | null>(null);

  useEffect(() => {
    const handleMessage = (event: MessageEvent) => {
      const data = JSON.parse(event.data);
      if (data.type === "open_orders_update") {
        setOpenOrders(data.data.result);
      } else if (data.type === "cancel_response") {
        if (data.error) {
          setMessage({
            type: "error",
            text: `Failed to cancel order: ${data.error}`,
          });
        } else {
          setMessage({ type: "success", text: "Order cancelled successfully" });
        }
      }
    };

    ws?.addEventListener("message", handleMessage);

    // Request initial open orders
    ws?.send(JSON.stringify({ type: "get_open_orders" }));

    return () => {
      ws?.removeEventListener("message", handleMessage);
    };
  }, [ws]);

  const handleModifyClick = (order: Order) => {
    setSelectedOrder(order);
  };

  const handleCloseModifyForm = () => {
    setSelectedOrder(null);
  };

  const handleCancelClick = (orderId: string) => {
    ws?.send(
      JSON.stringify({ type: "cancel_order", data: { order_id: orderId } })
    );
  };

  return (
    <div className='mt-6 space-y-4'>
      {message && (
        <Alert variant={message.type === "success" ? "default" : "destructive"}>
          <AlertTitle>
            {message.type === "success" ? "Success" : "Error"}
          </AlertTitle>
          <AlertDescription>{message.text}</AlertDescription>
        </Alert>
      )}
      {openOrders.length === 0 ? (
        <p>No open orders</p>
      ) : (
        <ScrollArea className='h-[400px]'>
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>Instrument</TableHead>
                <TableHead>Amount</TableHead>
                <TableHead>Price</TableHead>
                <TableHead>Direction</TableHead>
                <TableHead>Type</TableHead>
                <TableHead>Actions</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {openOrders.map((order) => (
                <TableRow key={order.order_id}>
                  <TableCell>{order.instrument_name}</TableCell>
                  <TableCell>{order.amount}</TableCell>
                  <TableCell>{order.price}</TableCell>
                  <TableCell>{order.direction}</TableCell>
                  <TableCell>{order.order_type}</TableCell>
                  <TableCell>
                    <Dialog>
                      <DialogTrigger asChild>
                        <Button
                          variant='outline'
                          size='sm'
                          className='mr-2'
                          onClick={() => handleModifyClick(order)}
                        >
                          Modify
                        </Button>
                      </DialogTrigger>
                      <DialogContent>
                        <ModifyOrderForm
                          ws={ws}
                          orderId={order.order_id}
                          currentAmount={order.amount}
                          currentPrice={order.price}
                        />
                      </DialogContent>
                    </Dialog>
                    <Button
                      variant='destructive'
                      size='sm'
                      onClick={() => handleCancelClick(order.order_id)}
                    >
                      Cancel
                    </Button>
                  </TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        </ScrollArea>
      )}
    </div>
  );
}
